#include "MidiExportJob.h"

#include "DuomiClient.h"

namespace duomi {
bool MidiExportJob::start(juce::File source, juce::File destination) {
  if (isThreadRunning()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    state = {};
    input = std::move(source);
    output = std::move(destination);
    state.busy = true;
    state.message = juce::String::fromUTF8("正在导出 MIDI…");
  }
  if (!startThread()) {
    std::lock_guard<std::mutex> lock(mutex);
    state.busy = false;
    state.failed = true;
    state.message = juce::String::fromUTF8("无法启动 MIDI 导出线程");
    sendChangeMessage();
    return false;
  }
  sendChangeMessage();
  return true;
}
MidiExportJob::Snapshot MidiExportJob::getSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex);
  auto result = state;
  result.busy = result.busy || isThreadRunning();
  return result;
}
juce::Result MidiExportJob::copyMidi() {
  if (input == juce::File{} || !input.existsAsFile())
    return juce::Result::fail(
        juce::String::fromUTF8("原 MIDI 文件不存在，请重新生成或打开文件。"));
  if (output == juce::File{} || !output.hasFileExtension("mid;midi"))
    return juce::Result::fail(
        juce::String::fromUTF8("导出文件名必须以 .mid 或 .midi 结尾。"));
  if (input.getSize() > 16 * 1024 * 1024)
    return juce::Result::fail(
        juce::String::fromUTF8("MIDI 文件超过 16 MB，无法导出。"));
  juce::MemoryBlock bytes;
  if (!input.loadFileAsData(bytes))
    return juce::Result::fail(juce::String::fromUTF8("无法读取原 MIDI 文件。"));
  // Preserve raw ticks, tempo map, tracks and metadata. Validate but do not
  // serialise the seconds-based preview sequence back into a MIDI file.
  GeneratedMidi parsed;
  const auto valid = DuomiClient::parseMidi(bytes, parsed);
  if (valid.failed()) return valid;
  if (input == output) return juce::Result::ok();
  if (threadShouldExit())
    return juce::Result::fail(juce::String::fromUTF8("导出已取消"));
  if (!output.getParentDirectory().isDirectory())
    return juce::Result::fail(
        juce::String::fromUTF8("目标目录不存在，请重新选择保存位置。"));
  juce::TemporaryFile temporary(output);
  if (!temporary.getFile().replaceWithData(bytes.getData(), bytes.getSize()))
    return juce::Result::fail(
        juce::String::fromUTF8("无法写入目标目录，请检查权限和磁盘空间。"));
  if (threadShouldExit())
    return juce::Result::fail(juce::String::fromUTF8("导出已取消"));
  if (!temporary.overwriteTargetFileWithTemporary())
    return juce::Result::fail(juce::String::fromUTF8(
        "无法保存目标 MIDI，请检查文件占用及目录权限。"));
  return juce::Result::ok();
}
void MidiExportJob::run() {
  auto result = juce::Result::fail(juce::String::fromUTF8("MIDI 导出失败"));
  try {
    result = copyMidi();
  } catch (const std::exception&) {
  }
  {
    std::lock_guard<std::mutex> lock(mutex);
    state.busy = false;
    state.failed = result.failed();
    state.file = result.wasOk() ? output : juce::File{};
    state.message = result.wasOk() ? juce::String::fromUTF8("已导出：") +
                                         output.getFullPathName()
                                   : result.getErrorMessage();
  }
  sendChangeMessage();
}
}  // namespace duomi
