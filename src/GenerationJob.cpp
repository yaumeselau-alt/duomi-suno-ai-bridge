#include "GenerationJob.h"

namespace duomi {
GenerationJob::GenerationJob(ClientOptions options, DuomiClient::Transport http)
    : Thread("Duomi generation"),
      clientOptions(std::move(options)),
      transport(std::move(http)) {}

GenerationJob::~GenerationJob() {
  cancel();
  // Cancel native HTTP before joining. Never force-kill a worker.
  waitForThreadToExit();
}

bool GenerationJob::start(juce::String key, juce::String prompt, bool resume) {
  if (isThreadRunning()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    juce::String validation;
    if (key.trim().isEmpty())
      validation = juce::String::fromUTF8(
          "请输入 API Key；也可打开本地 MIDI 或音频免费试听。");
    else if (key.containsAnyOf("\r\n") || key.trim().length() > 4096)
      validation =
          juce::String::fromUTF8("API Key 格式无效，请重新粘贴完整密钥。");
    else if (!resume && prompt.trim().isEmpty())
      validation =
          juce::String::fromUTF8("请填写音乐描述，例如：轻快的钢琴，C 大调。");
    else if (resume && state.taskId.isEmpty() && state.midiTaskId.isEmpty())
      validation =
          juce::String::fromUTF8("没有可继续查询的任务，请先生成 MIDI。");
    if (validation.isNotEmpty()) {
      state.status = validation;
      state.failed = true;
      sendChangeMessage();
      return false;
    }
    apiKey = key.trim();
    fileToImport = {};
    restoredBytes.reset();
    description = std::move(prompt);
    resumeTaskId = resume ? state.taskId : juce::String{};
    resumeMidiTaskId = resume ? state.midiTaskId : juce::String{};
    resumeAudioUrl = resume ? state.audioUrl : juce::String{};
    state = {};
    state.taskId = resumeTaskId;
    state.midiTaskId = resumeMidiTaskId;
    state.audioUrl = resumeAudioUrl;
    state.busy = true;
    state.status = resume ? juce::String::fromUTF8("正在继续查询已有任务…")
                          : juce::String::fromUTF8("生成中…");
    state.logFile = clientOptions.outputDirectory.getChildFile(
        "request_" + juce::Uuid().toString() + ".log");
    cancellation = std::make_shared<Cancellation>();
  }
  if (!startThread()) {
    std::lock_guard<std::mutex> lock(mutex);
    apiKey.clear();
    state.busy = false;
    state.failed = true;
    state.status = juce::String::fromUTF8("无法启动后台请求线程");
    sendChangeMessage();
    return false;
  }
  sendChangeMessage();
  return true;
}

void GenerationJob::cancel() {
  std::shared_ptr<Cancellation> token;
  {
    std::lock_guard<std::mutex> lock(mutex);
    token = cancellation;
  }
  if (token) token->cancel();
}

bool GenerationJob::importMidi(juce::File file) {
  if (isThreadRunning()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    state = {};
    state.busy = true;
    state.status = juce::String::fromUTF8("正在读取 MIDI…");
    fileToImport = std::move(file);
    restoredBytes.reset();
    apiKey.clear();
    resumeTaskId.clear();
    resumeMidiTaskId.clear();
    resumeAudioUrl.clear();
    cancellation = std::make_shared<Cancellation>();
  }
  if (!startThread()) {
    std::lock_guard<std::mutex> lock(mutex);
    state.busy = false;
    state.failed = true;
    state.status = juce::String::fromUTF8("无法启动 MIDI 读取线程");
    sendChangeMessage();
    return false;
  }
  sendChangeMessage();
  return true;
}

GenerationJob::Snapshot GenerationJob::getSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex);
  auto copy = state;
  copy.busy = state.busy || isThreadRunning();
  copy.resumeAvailable =
      state.resumeAvailable || (!state.midi && (state.taskId.isNotEmpty() ||
                                                state.midiTaskId.isNotEmpty()));
  return copy;
}

bool GenerationJob::restore(Snapshot metadata, juce::MemoryBlock bytes,
                            juce::String name) {
  if (isThreadRunning()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    state = std::move(metadata);
    state.midi.reset();
    state.failed = false;
    state.busy = bytes.getSize() != 0;
    state.status = juce::String::fromUTF8(
        state.busy ? "正在恢复工程 MIDI…"
                   : (state.taskId.isNotEmpty() || state.midiTaskId.isNotEmpty()
                          ? "已恢复任务；点击继续查询时才联网。"
                          : "工程设置已恢复"));
    apiKey.clear();
    fileToImport = {};
    restoredBytes = std::move(bytes);
    restoredName = std::move(name);
    resumeAudioUrl = state.audioUrl;
    cancellation = std::make_shared<Cancellation>();
  }
  if (restoredBytes.getSize() != 0 && !startThread()) {
    std::lock_guard<std::mutex> lock(mutex);
    state.busy = false;
    state.failed = true;
    state.status = juce::String::fromUTF8("无法启动 MIDI 恢复线程");
    sendChangeMessage();
    return false;
  }
  sendChangeMessage();
  return true;
}

void GenerationJob::run() {
  ClientOptions options = clientOptions;
  options.logFile = state.logFile;
  auto midi = std::make_shared<GeneratedMidi>();
  auto result = juce::Result::fail(juce::String::fromUTF8("请求异常中断"));
  try {
    if (restoredBytes.getSize() != 0) {
      result = DuomiClient::parseMidi(restoredBytes, *midi);
      if (result.wasOk() && !cancellation->isCancelled()) {
        result = options.outputDirectory.createDirectory();
        if (result.wasOk()) {
          const auto name =
              juce::File::createLegalFileName(restoredName).substring(0, 120);
          midi->file =
              options.outputDirectory
                  .getChildFile(
                      "restored-" + juce::Uuid().toString() + "-" +
                      (name.isEmpty() ? juce::String("preview.mid") : name))
                  .withFileExtension(".mid");
          if (!midi->file.replaceWithData(restoredBytes.getData(),
                                          restoredBytes.getSize()))
            result = juce::Result::fail(
                juce::String::fromUTF8("无法恢复 MIDI 文件，请检查目录权限"));
        }
      }
    } else if (fileToImport != juce::File{}) {
      juce::MemoryBlock bytes;
      if (fileToImport.getSize() > 16 * 1024 * 1024 ||
          !fileToImport.loadFileAsData(bytes))
        result = juce::Result::fail(
            juce::String::fromUTF8("无法读取 MIDI 文件或文件超过 16 MB"));
      else if (cancellation->isCancelled())
        result = juce::Result::fail(juce::String::fromUTF8("已取消"));
      else {
        result = DuomiClient::parseMidi(bytes, *midi);
        midi->file = fileToImport;
      }
    } else {
      DuomiClient client(options, transport);
      result = client.generate(
          apiKey, description, *cancellation, *midi,
          [this, midi](const juce::String& status, const juce::String& taskId) {
            {
              std::lock_guard<std::mutex> lock(mutex);
              state.status = status;
              state.taskId = taskId;
              state.midiTaskId = midi->midiTaskId;
              if (midi->audioUrl.isNotEmpty()) state.audioUrl = midi->audioUrl;
            }
            sendChangeMessage();
          },
          resumeTaskId, resumeMidiTaskId);
    }
  } catch (const std::exception&) {
    result =
        juce::Result::fail(juce::String::fromUTF8("后台请求异常，请查看日志"));
  }
  apiKey.clear();
  if ((fileToImport != juce::File{} || restoredBytes.getSize() != 0) &&
      cancellation->isCancelled())
    result = juce::Result::fail(juce::String::fromUTF8("已取消"));
  if (midi->audioUrl.isEmpty()) midi->audioUrl = resumeAudioUrl;
  {
    std::lock_guard<std::mutex> lock(mutex);
    state.busy = false;
    state.failed = result.failed() && !cancellation->isCancelled();
    if (result.failed()) state.status = result.getErrorMessage();
    if (result.wasOk()) {
      state.status = juce::String::fromUTF8("MIDI 已就绪：") +
                     juce::String(midi->noteCount) +
                     juce::String::fromUTF8(" 个音符");
      state.midi = std::move(midi);
    }
  }
  sendChangeMessage();
}
}  // namespace duomi
