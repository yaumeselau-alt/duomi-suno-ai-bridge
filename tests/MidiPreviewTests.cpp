#include <cmath>
#include <iostream>
#include <stdexcept>

#include "PluginProcessor.h"

namespace {
using duomi::MidiPreviewPlayer;
using State = MidiPreviewPlayer::State;
void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
std::shared_ptr<duomi::GeneratedMidi> clip(double start = 0.01,
                                           double end = 0.2, int voices = 1) {
  auto midi = std::make_shared<duomi::GeneratedMidi>();
  for (int i = 0; i < voices; ++i) {
    auto on = juce::MidiMessage::noteOn(1, 48 + i, 0.8f);
    on.setTimeStamp(start);
    auto off = juce::MidiMessage::noteOff(1, 48 + i);
    off.setTimeStamp(end);
    midi->sequence.addEvent(on);
    midi->sequence.addEvent(off);
  }
  midi->sequence.updateMatchedPairs();
  midi->noteCount = voices;
  midi->durationSeconds = end;
  return midi;
}
bool waitForJob(duomi::GenerationJob& job) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
  while (job.getSnapshot().busy &&
         juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::Thread::sleep(5);
  return !job.getSnapshot().busy;
}
double magnitude(const juce::AudioBuffer<float>& buffer) {
  return buffer.getMagnitude(0, buffer.getNumSamples());
}
void finite(const juce::AudioBuffer<float>& buffer) {
  for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    for (int i = 0; i < buffer.getNumSamples(); ++i)
      check(std::isfinite(buffer.getSample(ch, i)) &&
                std::abs(buffer.getSample(ch, i)) <= 1.0f,
            "Non-finite or out-of-range audio");
}
std::vector<float> renderClip(int blockSize, double rate) {
  MidiPreviewPlayer player;
  player.prepare(rate);
  player.load(clip());
  player.play();
  std::vector<float> output;
  for (int remaining = static_cast<int>(rate * 0.5); remaining > 0;) {
    const int count = juce::jmin(blockSize, remaining);
    juce::AudioBuffer<float> buffer(2, count);
    player.render(buffer);
    finite(buffer);
    output.insert(output.end(), buffer.getReadPointer(0),
                  buffer.getReadPointer(0) + count);
    remaining -= count;
  }
  check(player.getSnapshot().state == State::finished, "Song did not finish");
  return output;
}
}  // namespace

int runMidiPreviewTests() {
  int passed = 0;
  auto test = [&](const char* name, auto fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << std::endl;
  };
  test("sample scheduling and block-size independence at 44.1/48/96 kHz", [] {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
      const auto a = renderClip(64, rate), b = renderClip(511, rate);
      check(a.size() == b.size(), "Render size mismatch");
      double peak = 0.0;
      for (size_t i = 0; i < a.size(); ++i) {
        check(std::abs(a[i] - b[i]) < 0.000001f,
              "Block size changes event timing or audio");
        if (i < static_cast<size_t>(std::llround(rate * 0.01)))
          check(a[i] == 0.0f, "Early note-on");
        peak = juce::jmax(peak, static_cast<double>(std::abs(a[i])));
      }
      check(peak > 0.01, "Silent synthesiser");
    }
  });
  test("events exactly on block boundaries", [] {
    MidiPreviewPlayer player;
    player.prepare(48000);
    player.load(clip(256.0 / 48000, 512.0 / 48000));
    player.play();
    juce::AudioBuffer<float> buffer(1, 256);
    player.render(buffer);
    check(magnitude(buffer) == 0.0, "Boundary event started early");
    player.render(buffer);
    check(magnitude(buffer) > 0.01, "Boundary event missing");
    for (int i = 0; i < 30; ++i) player.render(buffer);
    check(player.getSnapshot().state == State::finished,
          "Boundary note-off missing");
  });
  test("pause freezes position and voices; resume; stop resets", [] {
    MidiPreviewPlayer player;
    player.prepare(48000);
    player.load(clip(0, 2));
    player.play();
    juce::AudioBuffer<float> buffer(2, 512);
    player.render(buffer);
    const auto before = player.getSnapshot().positionSeconds;
    player.pause();
    player.render(buffer);
    player.render(buffer);
    check(player.getSnapshot().state == State::paused &&
              player.getSnapshot().positionSeconds == before,
          "Pause advanced playhead");
    check(magnitude(buffer) == 0.0, "Pause did not become silent");
    player.play();
    player.render(buffer);
    check(magnitude(buffer) > 0.01 &&
              player.getSnapshot().positionSeconds > before,
          "Held note not resumed");
    player.stop();
    player.render(buffer);
    player.render(buffer);
    check(magnitude(buffer) == 0.0 &&
              player.getSnapshot().positionSeconds == 0.0 &&
              player.getSnapshot().state == State::stopped,
          "Stop did not reset");
    player.play();
    player.render(buffer);
    check(magnitude(buffer) > 0.01, "Restart failed");
  });
  test("rapid stop/play and repeated play", [] {
    MidiPreviewPlayer player;
    player.prepare(48000);
    player.load(clip(0, 2));
    player.play();
    juce::AudioBuffer<float> buffer(1, 512);
    player.render(buffer);
    player.render(buffer);
    player.stop();
    player.play();
    player.render(buffer);
    check(std::abs(player.getSnapshot().positionSeconds - 512.0 / 48000) < 1e-9,
          "Rapid stop/play lost reset");
    player.play();
    player.render(buffer);
    check(
        std::abs(player.getSnapshot().positionSeconds - 1024.0 / 48000) < 1e-9,
        "Repeated play restarted");
  });
  test("polyphony, sustain end release and replay", [] {
    auto midi = clip(0, 0.1, 48);
    auto pedal = juce::MidiMessage::controllerEvent(1, 64, 127);
    pedal.setTimeStamp(0);
    midi->sequence.addEvent(pedal);
    midi->sequence.updateMatchedPairs();
    MidiPreviewPlayer player;
    player.prepare(48000);
    player.load(midi);
    player.play();
    juce::AudioBuffer<float> buffer(2, 512);
    for (int i = 0; i < 60; ++i) {
      player.render(buffer);
      finite(buffer);
    }
    check(
        player.getSnapshot().state == State::finished && magnitude(buffer) == 0,
        "Sustain hangs at song end");
    player.play();
    player.render(buffer);
    check(
        magnitude(buffer) > 0.01 && player.getSnapshot().positionSeconds < 0.02,
        "Finished song cannot replay");
  });
  test("clip ownership, replacement and unload", [] {
    MidiPreviewPlayer player;
    player.prepare(48000);
    auto first = clip(0, 2);
    std::weak_ptr<const duomi::GeneratedMidi> weak = first;
    player.load(first);
    first.reset();
    player.play();
    juce::AudioBuffer<float> buffer(1, 512);
    player.render(buffer);
    player.load(clip(0, 1));
    player.play();
    player.render(buffer);
    check(!weak.expired(), "Audio thread destroyed retired MIDI");
    player.collectRetired();
    check(weak.expired(), "Control thread did not reclaim MIDI");
    player.load(clip(0, 0.5));
    player.load(clip(0, 0.25));
    player.play();
    player.render(buffer);
    check(player.getSnapshot().durationSeconds == 0.25,
          "Superseded pending clip played");
    player.load(nullptr);
    player.render(buffer);
    player.render(buffer);
    check(
        magnitude(buffer) == 0 && player.getSnapshot().state == State::stopped,
        "Unload left hanging sound");
  });
  test("prepare/release, mono/stereo and empty blocks", [] {
    MidiPreviewPlayer player;
    player.prepare(48000);
    player.load(clip(0, 2));
    player.play();
    juce::AudioBuffer<float> buffer(2, 512);
    player.render(buffer);
    for (int i = 0; i < 512; ++i)
      check(buffer.getSample(0, i) == buffer.getSample(1, i),
            "Stereo mismatch");
    player.release();
    player.render(buffer);
    check(magnitude(buffer) == 0, "Release rendered audio");
    player.prepare(44100);
    player.play();
    player.render(buffer);
    check(magnitude(buffer) > 0.01, "Reprepare failed");
    juce::AudioBuffer<float> empty(0, 0);
    player.render(empty);
    player.prepare(0);
    player.render(buffer);
    check(magnitude(buffer) == 0, "Invalid sample rate rendered");
  });
  test("processor integration and editor lifetime", [] {
    DuomiSunoAIBridgeAudioProcessor processor;
    processor.prepareToPlay(48000, 512);
    processor.setPreviewMidi(clip(0, 2));
    {
      std::unique_ptr<juce::AudioProcessorEditor> editor(
          processor.createEditor());
    }
    processor.getPreviewPlayer().play();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    check(magnitude(buffer) > 0.01, "Editor closure stopped player");
    processor.releaseResources();
  });
  test("background local MIDI import and invalid file", [] {
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(
        "preview-test-results");
    check(folder.createDirectory().wasOk(), "Cannot create test folder");
    auto file = folder.getChildFile("import.mid");
    {
      juce::MidiFile midi;
      midi.setTicksPerQuarterNote(960);
      juce::MidiMessageSequence seq;
      auto on = juce::MidiMessage::noteOn(1, 60, 0.8f);
      auto off = juce::MidiMessage::noteOff(1, 60);
      off.setTimeStamp(960);
      seq.addEvent(on);
      seq.addEvent(off);
      midi.addTrack(seq);
      juce::FileOutputStream stream(file);
      stream.setPosition(0);
      stream.truncate();
      check(midi.writeTo(stream), "Cannot write test MIDI");
    }
    duomi::GenerationJob job;
    check(job.importMidi(file), "Import refused");
    check(waitForJob(job), "Import timed out");
    check(job.getSnapshot().midi && job.getSnapshot().midi->noteCount == 1,
          "Import failed");
    check(job.importMidi(folder.getChildFile("absent.mid")),
          "Invalid import refused unexpectedly");
    check(waitForJob(job) && job.getSnapshot().failed,
          "Missing file not reported");
  });
  std::cout << passed
            << " MIDI preview scenarios passed. No network requests.\n";
  return 0;
}

int renderMidiPreview(const juce::File& input, const juce::File& output) {
  juce::MemoryBlock bytes;
  check(input.loadFileAsData(bytes), "Cannot read input MIDI");
  auto midi = std::make_shared<duomi::GeneratedMidi>();
  const auto result = duomi::DuomiClient::parseMidi(bytes, *midi);
  check(result.wasOk(), result.getErrorMessage().toRawUTF8());
  midi->file = input;
  check(output.getParentDirectory().createDirectory().wasOk(),
        "Cannot create output directory");
  std::unique_ptr<juce::OutputStream> stream(output.createOutputStream());
  check(stream != nullptr, "Cannot create WAV");
  stream->setPosition(0);
  static_cast<juce::FileOutputStream*>(stream.get())->truncate();
  juce::WavAudioFormat format;
  auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                   .withSampleRate(48000)
                                                   .withNumChannels(2)
                                                   .withBitsPerSample(16));
  check(writer != nullptr, "Cannot create WAV writer");
  DuomiSunoAIBridgeAudioProcessor processor;
  processor.prepareToPlay(48000, 512);
  check(processor.getGenerationJob().importMidi(input),
        "Real MIDI import refused");
  check(waitForJob(processor.getGenerationJob()), "Real MIDI import timed out");
  processor.getGenerationJob().sendSynchronousChangeMessage();
  check(processor.getPreviewMidi() &&
            processor.getPreviewMidi()->noteCount == midi->noteCount,
        "Background import not connected to processor");
  processor.getPreviewPlayer().play();
  juce::AudioBuffer<float> buffer(2, 512);
  juce::MidiBuffer events;
  double peak = 0, squares = 0;
  juce::int64 samples = 0;
  bool screenshot = false;
  const auto limit =
      static_cast<juce::int64>((midi->durationSeconds + 2) * 48000);
  while (samples < limit) {
    processor.processBlock(buffer, events);
    finite(buffer);
    check(writer->writeFromAudioSampleBuffer(buffer, 0, 512),
          "WAV write failed");
    peak = juce::jmax(peak, magnitude(buffer));
    for (int i = 0; i < 512; ++i)
      squares += buffer.getSample(0, i) * buffer.getSample(0, i);
    samples += 512;
    if (!screenshot && samples >= 3 * 48000) {
      std::unique_ptr<juce::AudioProcessorEditor> editor(
          processor.createEditor());
      auto png = output.withFileExtension(".png");
      juce::FileOutputStream imageStream(png);
      imageStream.setPosition(0);
      imageStream.truncate();
      check(juce::PNGImageFormat().writeImageToStream(
                editor->createComponentSnapshot(editor->getLocalBounds()),
                imageStream),
            "Screenshot write failed");
      screenshot = true;
    }
    if (processor.getPreviewPlayer().getSnapshot().state == State::finished)
      break;
  }
  check(processor.getPreviewPlayer().getSnapshot().state == State::finished,
        "Real song did not finish");
  check(peak > 0.01, "Real MIDI render silent");
  processor.releaseResources();
  writer.reset();
  std::cout << "Rendered " << midi->noteCount
            << " notes, MIDI seconds=" << midi->durationSeconds
            << ", WAV seconds=" << samples / 48000.0 << ", peak=" << peak
            << ", RMS=" << std::sqrt(squares / samples)
            << "\nWAV: " << output.getFullPathName() << std::endl;
  return 0;
}
