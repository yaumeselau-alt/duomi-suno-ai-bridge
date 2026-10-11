#include "MidiTrackJob.h"
namespace duomi {
bool MidiTrackJob::start(std::shared_ptr<const GeneratedMidi> source,
                         int index) {
  if (isThreadRunning() || !source) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    state = {};
    state.busy = true;
    state.source = std::move(source);
    state.index = index;
  }
  if (startThread()) return true;
  std::lock_guard<std::mutex> lock(mutex);
  state.busy = false;
  state.failed = true;
  state.message = juce::String::fromUTF8("无法启动 MIDI 分轨");
  return false;
}
juce::Result MidiTrackJob::extract(const GeneratedMidi& source, int index,
                                   juce::MemoryBlock& bytes) {
  bytes.reset();
  if (index < 1 || index > (int)source.parts.size())
    return juce::Result::fail("Invalid MIDI track");
  juce::MidiFile original, result;
  juce::MemoryInputStream input(source.fileBytes, false);
  if (!original.readFrom(input)) return juce::Result::fail("Invalid MIDI");
  const auto& part = source.parts[(size_t)index - 1];
  if (part.track < 0 || part.track >= original.getNumTracks())
    return juce::Result::fail("Missing MIDI track");
  const auto format = original.getTimeFormat();
  if (format > 0)
    result.setTicksPerQuarterNote(format);
  else
    result.setSmpteTimeFormat(-(format >> 8), format & 0xff);
  juce::MidiMessageSequence globals, chosen;
  // Global tempo/meter may live on any physical track. Preserve absolute time,
  // initial silence, controllers and original PPQ/SMPTE without re-quantising.
  for (int t = 0; t < original.getNumTracks(); ++t)
    for (const auto* e : *original.getTrack(t)) {
      const auto& m = e->message;
      if (m.isTempoMetaEvent() || m.isTimeSignatureMetaEvent() ||
          m.isKeySignatureMetaEvent())
        globals.addEvent(m);
      else if (t == part.track &&
               ((m.getChannel() == part.channel) ||
                (m.isMetaEvent() && !m.isEndOfTrackMetaEvent())))
        chosen.addEvent(m);
    }
  result.addTrack(globals);
  result.addTrack(chosen);
  juce::MemoryOutputStream output;
  if (!result.writeTo(output))
    return juce::Result::fail("Cannot encode MIDI track");
  bytes = output.getMemoryBlock();
  return juce::Result::ok();
}
void MidiTrackJob::run() {
  auto local = getSnapshot();
  auto result = std::make_shared<GeneratedMidi>();
  juce::MemoryBlock bytes;
  auto status = juce::Result::fail("MIDI track failed");
  try {
    status = extract(*local.source, local.index, bytes);
    if (status.wasOk()) status = DuomiClient::parseMidi(bytes, *result);
    if (status.wasOk() && !threadShouldExit()) {
      auto dir = juce::File::getSpecialLocation(
                     juce::File::userApplicationDataDirectory)
                     .getChildFile("DuomiSunoAIBridge")
                     .getChildFile("MidiTracks");
      status = dir.createDirectory();
      if (status.wasOk()) {
        result->file = dir.getChildFile("Track-" + juce::String(local.index) +
                                        "-" + juce::Uuid().toString() + ".mid");
        if (!result->file.replaceWithData(bytes.getData(), bytes.getSize()))
          status = juce::Result::fail("Cannot save MIDI track");
      }
      result->audioUrl = local.source->audioUrl;
    }
  } catch (...) {
    status = juce::Result::fail("MIDI track exceeded resources");
  }
  std::lock_guard<std::mutex> lock(mutex);
  state.busy = false;
  if (threadShouldExit()) {
    if (result->file != juce::File{}) result->file.deleteFile();
    state.message = juce::String::fromUTF8("分轨提取已取消");
    return;
  }
  state.failed = status.failed();
  state.message = status.failed()
                      ? status.getErrorMessage()
                      : juce::String::fromUTF8("已提取轨道，可试听或拖出。");
  if (status.wasOk()) state.midi = result;
}
}  // namespace duomi