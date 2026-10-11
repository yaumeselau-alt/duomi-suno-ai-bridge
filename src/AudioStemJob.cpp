#include "AudioStemJob.h"

#include <stdexcept>

#include "SessionState.h"
namespace duomi {
bool AudioStemJob::start(juce::String k, juce::String source) {
  if (isThreadRunning()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.busy) return false;
    if (k.trim().isEmpty() || k.containsAnyOf("\r\n") || source.isEmpty()) {
      state.failed = true;
      state.message =
          juce::String::fromUTF8("请先加载生成的成品音频并填写 Key。");
      return false;
    }
    if (state.batch.sourceTask != source) {
      state = {};
      state.batch.sourceTask = source;
    }
    key = std::move(k);
    token = std::make_shared<Cancellation>();
    state.busy = true;
    state.failed = false;
    state.message = juce::String::fromUTF8("正在读取或分离乐器轨道…");
  }
  if (startThread()) return true;
  std::lock_guard<std::mutex> lock(mutex);
  state.busy = false;
  state.failed = true;
  state.message = juce::String::fromUTF8("无法启动分轨线程");
  return false;
}
void AudioStemJob::run() {
  auto batch = getSnapshot().batch;
  auto result = juce::Result::fail("Stem separation failed");
  try {
    juce::InterProcessLock serialise("Duomi-" + cacheName(batch.sourceTask));
    if (!serialise.enter(0))
      throw std::runtime_error("同一首歌正在另一实例中分轨，请稍后继续查询。");
    struct Unlock {
      juce::InterProcessLock& lock;
      ~Unlock() { lock.exit(); }
    } unlock{serialise};
    const auto file = cacheDirectory.getChildFile(cacheName(batch.sourceTask));
    if (file.exists() && batch.stems.empty() && !batch.submissionUncertain) {
      SessionState saved;
      juce::MemoryBlock bytes;
      if (file.getSize() > 1024 * 1024 || !file.loadFileAsData(bytes) ||
          !SessionState::decode(bytes.getData(), (int)bytes.getSize(), saved) ||
          saved.audioSourceTask != batch.sourceTask)
        throw std::runtime_error(
            "分轨缓存无法读取，已停止新提交，请先核对已有云端任务。");
      batch = std::move(saved.stemBatch);
    }
    const auto persist = [&] {
      SessionState saved;
      saved.audioSourceTask = batch.sourceTask;
      saved.stemBatch = batch;
      const auto bytes = saved.encode();
      if (cacheDirectory.createDirectory().failed())
        throw std::runtime_error("无法保存分轨任务，请检查目录权限。");
      juce::TemporaryFile temp(file);
      if (!temp.getFile().replaceWithData(bytes.getData(), bytes.getSize()) ||
          !temp.overwriteTargetFileWithTemporary())
        throw std::runtime_error("无法保存分轨任务，已停止提交或查询。");
    };
    auto settings = options;
    if (settings.logFile == juce::File{})
      settings.logFile = file.withFileExtension(".log");
    result =
        DuomiClient(settings, transport)
            .separateStems(key, *token, batch, [&](const juce::String& msg) {
              // Save IDs/uncertain state before the billable POST and after
              // publication.
              persist();
              std::lock_guard<std::mutex> lock(mutex);
              state.batch = batch;
              state.message = DuomiClient::redact(msg, key);
            });
    persist();
  } catch (const std::exception& e) {
    result = juce::Result::fail(juce::String::fromUTF8(e.what()));
  } catch (...) {
    result = juce::Result::fail("Stem separation exceeded resources");
  }
  std::lock_guard<std::mutex> lock(mutex);
  state.batch = std::move(batch);
  state.busy = false;
  state.failed = result.failed();
  state.message = result.failed()
                      ? DuomiClient::redact(result.getErrorMessage(), key)
                      : juce::String::fromUTF8(
                            "分轨已就绪，请在轨道框选择乐器；切换不重新计费。");
  key.clear();
}
}  // namespace duomi