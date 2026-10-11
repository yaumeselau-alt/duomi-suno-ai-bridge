#include <iostream>
#include <stdexcept>

#include "AudioWaveformComponent.h"
#include "MidiDragButton.h"
#include "MidiExportJob.h"
#include "PianoRollComponent.h"
#include "PluginProcessor.h"

namespace {
void requireTransfer(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
bool waitExport(duomi::MidiExportJob& job) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
  while (job.getSnapshot().busy &&
         juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::Thread::sleep(5);
  return !job.getSnapshot().busy;
}
juce::MemoryBlock readBytes(const juce::File& file) {
  juce::MemoryBlock bytes;
  requireTransfer(file.loadFileAsData(bytes), "Cannot read test file");
  return bytes;
}
juce::File makeMidi(const juce::File& folder) {
  auto file = folder.getChildFile(juce::String::fromUTF8("中文 original.mid"));
  juce::MidiFile midi;
  midi.setTicksPerQuarterNote(960);
  juce::MidiMessageSequence tempo, notes;
  tempo.addEvent(juce::MidiMessage::tempoMetaEvent(600000));
  auto change = juce::MidiMessage::tempoMetaEvent(400000);
  change.setTimeStamp(960);
  tempo.addEvent(change);
  auto on = juce::MidiMessage::noteOn(1, 60, 0.8f);
  auto off = juce::MidiMessage::noteOff(1, 60);
  off.setTimeStamp(1920);
  notes.addEvent(juce::MidiMessage::textMetaEvent(3, "Original track"));
  notes.addEvent(on);
  notes.addEvent(off);
  midi.addTrack(tempo);
  midi.addTrack(notes);
  juce::FileOutputStream stream(file);
  requireTransfer(stream.openedOk() && midi.writeTo(stream),
                  "Cannot create test MIDI");
  return file;
}
juce::MouseEvent eventFor(juce::Component& component, float x, bool left = true,
                          float y = 10) {
  return {juce::Desktop::getInstance().getMainMouseSource(),
          {x, y},
          juce::ModifierKeys(left ? juce::ModifierKeys::leftButtonModifier
                                  : juce::ModifierKeys::rightButtonModifier),
          1,
          0,
          0,
          0,
          0,
          &component,
          &component,
          juce::Time::getCurrentTime(),
          {10, y},
          juce::Time::getCurrentTime(),
          1,
          x != 10};
}
}  // namespace

int runMidiTransferTests() {
  const auto folder = juce::File::getCurrentWorkingDirectory()
                          .getChildFile("transfer-test-results")
                          .getChildFile(juce::Uuid().toString());
  requireTransfer(folder.createDirectory().wasOk(),
                  "Cannot create test folder");
  const auto source = makeMidi(folder);
  const auto original = readBytes(source);
  int passed = 0;
  auto test = [&](const char* name, auto fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << std::endl;
  };
  test("export preserves exact bytes, tempo map, metadata and original", [&] {
    duomi::MidiExportJob job;
    const auto target =
        folder.getChildFile(juce::String::fromUTF8("导出 copy.mid"));
    requireTransfer(job.start(source, target) && waitExport(job),
                    "Export did not finish");
    requireTransfer(
        !job.getSnapshot().failed && job.getSnapshot().file == target,
        "Export failed");
    requireTransfer(
        readBytes(target) == original && readBytes(source) == original,
        "MIDI bytes changed");
    juce::MidiFile midi;
    juce::MemoryInputStream stream(original, false);
    requireTransfer(midi.readFrom(stream) && midi.getNumTracks() == 2 &&
                        midi.getTimeFormat() == 960,
                    "Fixture missing tracks or ticks");
  });
  test("overwrite after complete write; same source/destination", [&] {
    duomi::MidiExportJob job;
    const auto target = folder.getChildFile("replace.mid");
    requireTransfer(target.replaceWithText("previous bytes"),
                    "Cannot create destination");
    requireTransfer(job.start(source, target) && waitExport(job) &&
                        !job.getSnapshot().failed,
                    "Overwrite failed");
    requireTransfer(readBytes(target) == original, "Overwrite incomplete");
    requireTransfer(job.start(source, source) && waitExport(job) &&
                        !job.getSnapshot().failed,
                    "Same path failed");
    requireTransfer(readBytes(source) == original, "Same path damaged source");
  });
  test("missing/corrupt input leaves destination unchanged", [&] {
    duomi::MidiExportJob job;
    const auto target = folder.getChildFile("protected.mid");
    const juce::String saved = "keep these bytes";
    requireTransfer(target.replaceWithText(saved),
                    "Cannot create protected fixture");
    requireTransfer(job.start(folder.getChildFile("missing.mid"), target) &&
                        waitExport(job) && job.getSnapshot().failed,
                    "Missing file accepted");
    requireTransfer(target.loadFileAsString() == saved,
                    "Missing source harmed target");
    const auto corrupt = folder.getChildFile("corrupt.mid");
    corrupt.replaceWithText("not midi");
    requireTransfer(job.start(corrupt, target) && waitExport(job) &&
                        job.getSnapshot().failed,
                    "Corrupt MIDI accepted");
    requireTransfer(target.loadFileAsString() == saved,
                    "Invalid source harmed target");
  });
  test("bad extension and missing directory are reported", [&] {
    duomi::MidiExportJob job;
    requireTransfer(job.start(source, folder.getChildFile("bad.txt")) &&
                        waitExport(job) && job.getSnapshot().failed,
                    "Wrong extension accepted");
    requireTransfer(!folder.getChildFile("bad.txt").exists(),
                    "Invalid target written");
    requireTransfer(
        job.start(source,
                  folder.getChildFile("absent").getChildFile("copy.mid")) &&
            waitExport(job) && job.getSnapshot().failed,
        "Missing directory accepted");
  });
  test("drag threshold, copy-only and file captured at mouse-down", [&] {
    int calls = 0;
    juce::StringArray sent;
    bool canMove = true;
    std::function<void()> completion;
    MidiDragButton button([&](const juce::StringArray& files, bool move,
                              juce::Component* owner,
                              std::function<void()> cb) {
      ++calls;
      sent = files;
      canMove = move;
      completion = std::move(cb);
      requireTransfer(owner != nullptr, "Missing source component");
      return true;
    });
    button.setBounds(0, 0, 220, 32);
    button.setMidiFile(source);
    button.mouseDown(eventFor(button, 10));
    button.mouseDrag(eventFor(button, 14));
    requireTransfer(calls == 0, "Small movement started drag");
    button.setMidiFile(folder.getChildFile("new-clip.mid"));
    button.mouseDrag(eventFor(button, 20));
    button.mouseDrag(eventFor(button, 30));
    requireTransfer(calls == 1 && !canMove && sent.size() == 1 &&
                        sent[0] == source.getFullPathName(),
                    "Wrong drag payload, move permitted or repeated start");
    completion();
    button.mouseDrag(eventFor(button, 40));
    requireTransfer(calls == 1, "Gesture restarted after completion");
  });
  test("disabled, right-click and missing file do not start native drag", [&] {
    int calls = 0;
    bool failed = false;
    MidiDragButton button([&](const auto&, bool, auto*, auto) {
      ++calls;
      return true;
    });
    button.onStatus = [&](const auto&, bool error) { failed = error; };
    button.setBounds(0, 0, 220, 32);
    button.mouseDown(eventFor(button, 10));
    button.mouseDrag(eventFor(button, 30));
    button.setMidiFile(source);
    button.mouseDown(eventFor(button, 10, false));
    button.mouseDrag(eventFor(button, 30, false));
    requireTransfer(calls == 0, "Disabled or right-button drag started");
    button.setMidiFile(folder.getChildFile("missing.mid"));
    button.mouseDown(eventFor(button, 10));
    button.mouseDrag(eventFor(button, 30));
    requireTransfer(calls == 0 && failed, "Missing source not reported");
  });
  test("native start failure and completion after component destruction", [&] {
    bool failed = false;
    MidiDragButton rejected(
        [](const auto&, bool, auto*, auto) { return false; });
    rejected.onStatus = [&](const auto&, bool error) { failed = error; };
    rejected.setBounds(0, 0, 220, 32);
    rejected.setMidiFile(source);
    rejected.mouseDown(eventFor(rejected, 10));
    rejected.mouseDrag(eventFor(rejected, 30));
    requireTransfer(failed && rejected.isEnabled(),
                    "Start failure not recoverable");
    std::function<void()> completion;
    auto button = std::make_unique<MidiDragButton>(
        [&](const auto&, bool, auto*, auto cb) {
          completion = std::move(cb);
          return true;
        });
    button->setBounds(0, 0, 220, 32);
    button->setMidiFile(source);
    button->mouseDown(eventFor(*button, 10));
    button->mouseDrag(eventFor(*button, 30));
    requireTransfer(static_cast<bool>(completion),
                    "Missing completion callback");
    button.reset();
    completion();
  });
  test("editor closure keeps processor-owned export alive", [&] {
    DuomiSunoAIBridgeAudioProcessor processor;
    auto editor =
        std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    const auto target = folder.getChildFile("editor-closed.mid");
    requireTransfer(processor.getMidiExportJob().start(source, target),
                    "Processor export refused");
    editor.reset();
    requireTransfer(waitExport(processor.getMidiExportJob()) &&
                        !processor.getMidiExportJob().getSnapshot().failed,
                    "Closing editor stopped export");
    requireTransfer(readBytes(target) == original,
                    "Closed editor export changed bytes");
  });
  test("piano roll direct drag pins MIDI, uses copy and survives editor close",
       [&] {
         int calls = 0;
         juce::String sent;
         std::function<void()> complete;
         auto piano = std::make_unique<PianoRollComponent>(
             [&](const auto& files, bool move, auto*, auto cb) {
               ++calls;
               sent = files[0];
               requireTransfer(!move, "Piano drag allowed move");
               complete = std::move(cb);
               return true;
             });
         auto midi = std::make_shared<duomi::GeneratedMidi>();
         midi->file = source;
         piano->setMidi(midi);
         piano->mouseDown(eventFor(*piano, 10));
         piano->mouseDrag(eventFor(*piano, 14));
         requireTransfer(calls == 0, "Piano click started drag");
         auto replacement = std::make_shared<duomi::GeneratedMidi>();
         replacement->file = folder.getChildFile("replacement.mid");
         piano->setMidi(replacement);
         piano->mouseDrag(eventFor(*piano, 22));
         piano->mouseDrag(eventFor(*piano, 35));
         requireTransfer(calls == 1 && sent == source.getFullPathName(),
                         "Piano gesture changed source/repeated");
         piano.reset();
         complete();
         requireTransfer(source.existsAsFile(), "Piano drag removed MIDI");
       });
  test("waveform click seeks on release, drag exports audio without seeking",
       [&] {
         int calls = 0, seeks = 0;
         juce::String sent;
         std::function<void()> complete;
         AudioWaveformComponent wave(
             [&](const auto& files, bool move, auto*, auto cb) {
               ++calls;
               sent = files[0];
               requireTransfer(!move, "Wave drag allowed move");
               complete = std::move(cb);
               return true;
             });
         wave.setSize(400, 158);
         auto waveform =
             std::make_shared<duomi::AudioPreviewPlayer::Waveform>();
         waveform->durationSeconds = 1;
         waveform->peaks.resize(16);
         duomi::AudioPreviewPlayer::Snapshot snapshot;
         snapshot.waveform = waveform;
         snapshot.dragFile = source;
         wave.setPreview(snapshot);
         wave.onSeek = [&](double) { ++seeks; };
         wave.mouseDown(eventFor(wave, 14, true, 60));
         requireTransfer(seeks == 0, "Wave seek occurred on press");
         wave.mouseUp(eventFor(wave, 14, true, 60));
         requireTransfer(seeks == 1 && calls == 0, "Wave click failed");
         wave.mouseDown(eventFor(wave, 14, true, 60));
         snapshot.dragFile = folder.getChildFile("new.wav");
         wave.setPreview(snapshot);
         wave.mouseDrag(eventFor(wave, 25, true, 60));
         wave.mouseUp(eventFor(wave, 25, true, 60));
         requireTransfer(
             calls == 1 && seeks == 1 && sent == source.getFullPathName(),
             "Wave drag changed source or sought");
         complete();
         wave.mouseDown(eventFor(wave, 10, false, 60));
         wave.mouseDrag(eventFor(wave, 25, false, 60));
         wave.mouseUp(eventFor(wave, 25, false, 60));
         requireTransfer(calls == 1 && seeks == 1,
                         "Right click started operation");
         wave.mouseDown(eventFor(wave, 14, true, 60));
         wave.mouseDrag(eventFor(wave, 25, true, 60));
         wave.mouseUp(eventFor(wave, 25, true, 60));
         requireTransfer(calls == 1 && seeks == 1, "Missing drag file sought");
       });
  test("surface drag failure and empty content are safe", [&] {
    int errors = 0;
    PianoRollComponent piano(
        [](const auto&, bool, auto*, auto) { return false; });
    piano.onStatus = [&](const auto&, bool failed) {
      if (failed) ++errors;
    };
    piano.mouseDown(eventFor(piano, 10));
    piano.mouseDrag(eventFor(piano, 30));
    requireTransfer(errors == 1, "Empty surface missing error");
    auto midi = std::make_shared<duomi::GeneratedMidi>();
    midi->file = source;
    piano.setMidi(midi);
    piano.mouseDown(eventFor(piano, 10));
    piano.mouseDrag(eventFor(piano, 30));
    piano.mouseDown(eventFor(piano, 10));
    piano.mouseDrag(eventFor(piano, 30));
    requireTransfer(errors == 3, "Failed native drag blocked retry");
  });
  std::cout << passed
            << " MIDI transfer scenarios passed. Native drag transport is "
               "mocked; no Live acceptance claimed.\n";
  std::cout << "Test files: " << folder.getFullPathName() << std::endl;
  return 0;
}

int exportMidiCheck(const juce::File& source, const juce::File& destination) {
  const auto original = readBytes(source);
  duomi::MidiExportJob job;
  requireTransfer(job.start(source, destination) && waitExport(job),
                  "Export timed out");
  const auto state = job.getSnapshot();
  requireTransfer(!state.failed, state.message.toRawUTF8());
  requireTransfer(
      readBytes(destination) == original && readBytes(source) == original,
      "Export changed source or data");
  std::cout << "Export verified byte-for-byte: "
            << destination.getFullPathName() << ", " << original.getSize()
            << " bytes\n";
  return 0;
}
