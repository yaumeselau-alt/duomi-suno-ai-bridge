#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "PluginEditor.h"

namespace {
using namespace duomi;
void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
struct Folder {
  juce::File file =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("duomi-state-test-" + juce::Uuid().toString());
  Folder() {
    check(file.createDirectory().wasOk(), "Test folder creation failed");
  }
  ~Folder() { file.deleteRecursively(); }
};
std::shared_ptr<GeneratedMidi> fixture(bool arpeggio = false) {
  juce::MidiFile midi;
  midi.setTicksPerQuarterNote(960);
  juce::MidiMessageSequence track;
  track.addEvent(juce::MidiMessage::tempoMetaEvent(500000));
  const std::vector<std::vector<int>> chords{
      {60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62, 65}};
  for (int i = 0; i < 4; ++i)
    for (int n = 0; n < (int)chords[(size_t)i].size(); ++n) {
      auto on =
          juce::MidiMessage::noteOn(1, chords[(size_t)i][(size_t)n], 0.8f);
      auto off = juce::MidiMessage::noteOff(1, chords[(size_t)i][(size_t)n]);
      on.setTimeStamp(i * 1920 + (arpeggio ? n * 360 : 0));
      off.setTimeStamp(i * 1920 + (arpeggio ? n * 360 + 360 : 1920));
      track.addEvent(on);
      track.addEvent(off);
    }
  midi.addTrack(track);
  juce::MemoryOutputStream output;
  check(midi.writeTo(output), "Fixture encode failed");
  auto result = std::make_shared<GeneratedMidi>();
  check(DuomiClient::parseMidi(output.getMemoryBlock(), *result).wasOk(),
        "Fixture parse failed");
  return result;
}
void settle(DuomiSunoAIBridgeAudioProcessor& p) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 8000;
  do {
    p.refreshBackgroundState();
    if (!p.isRestoringSession() && !p.getChordJob().getSnapshot().busy &&
        !p.getGenerationJob().getSnapshot().busy &&
        !p.getAudioPreviewPlayer().getSnapshot().busy) {
      // Collect any final publication made just before worker exit.
      p.refreshBackgroundState();
      return;
    }
    juce::Thread::sleep(5);
  } while (juce::Time::getMillisecondCounterHiRes() < deadline);
  throw std::runtime_error("Session restore/analysis timed out");
}
void renderStopped(DuomiSunoAIBridgeAudioProcessor& p) {
  juce::AudioBuffer<float> b(2, 128);
  juce::MidiBuffer events;
  for (int i = 0; i < 4; ++i) p.processBlock(b, events);
  check(b.getMagnitude(0, 128) == 0, "Restoring unexpectedly started playback");
}
SessionState decode(const juce::MemoryBlock& bytes) {
  SessionState state;
  check(SessionState::decode(bytes.getData(), (int)bytes.getSize(), state),
        "State decode failed");
  return state;
}
}  // namespace

int runChordTests() {
  int passed = 0;
  const auto test = [&](const char* name, auto fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << std::endl;
  };
  test("block chords and arpeggios become independent piano chords", [&] {
    for (const bool arp : {false, true}) {
      const auto input = fixture(arp);
      const auto original = input->fileBytes;
      juce::MemoryBlock bytes;
      int count, uncertain, skipped;
      check(ChordExtractionJob::extract(*input, 120, 2, bytes, count, uncertain,
                                        skipped)
                .wasOk(),
            "Chord extraction failed");
      check(count == 4 && skipped == 0, "Wrong progression length");
      GeneratedMidi result;
      check(DuomiClient::parseMidi(bytes, result).wasOk() &&
                result.noteCount == 13,
            "Expected C-Am-F-G7 with 13 tones");
      std::vector<std::vector<int>> pitches(4);
      for (const auto* e : result.sequence)
        if (e->message.isNoteOn()) {
          const auto start = e->message.getTimeStamp();
          check(std::abs(start - std::round(start)) < 0.001,
                "Chord tones did not start together");
          const int index = juce::roundToInt(start);
          check(index >= 0 && index < 4 && e->noteOffObject &&
                    std::abs(e->noteOffObject->message.getTimeStamp() -
                             juce::jmin((double)index + 1,
                                        input->durationSeconds)) < 0.001,
                "Chord note ends not aligned");
          pitches[(size_t)index].push_back(e->message.getNoteNumber() % 12);
          check(e->message.getChannel() == 1, "Wrong melodic channel");
        }
      check(pitches[0] == std::vector<int>({0, 4, 7}) &&
                pitches[1] == std::vector<int>({9, 0, 4}) &&
                pitches[2] == std::vector<int>({5, 9, 0}) &&
                pitches[3] == std::vector<int>({7, 11, 2, 5}),
            "Known progression misidentified");
      check(input->fileBytes == original, "Source MIDI was modified");
      juce::MemoryInputStream stream(bytes, false);
      juce::MidiFile file;
      check(file.readFrom(stream), "Output file invalid");
      bool piano = false;
      for (int t = 0; t < file.getNumTracks(); ++t)
        for (const auto* e : *file.getTrack(t))
          piano |= e->message.isProgramChange() &&
                   e->message.getProgramChangeNumber() == 0;
      check(piano, "Piano program missing");
    }
  });
  test(
      "insufficient harmony, cancellation and bounds produce no invented "
      "chords",
      [&] {
        auto source = fixture();
        juce::MidiMessageSequence one;
        one.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f));
        auto off = juce::MidiMessage::noteOff(1, 60);
        off.setTimeStamp(2);
        one.addEvent(off);
        one.updateMatchedPairs();
        source->sequence = one;
        source->durationSeconds = 2;
        juce::MemoryBlock bytes;
        int n, u, s;
        check(ChordExtractionJob::extract(*source, 120, 2, bytes, n, u, s)
                      .failed() &&
                  bytes.getSize() == 0,
              "Single pitch invented a chord");
        source = fixture();
        check(
            ChordExtractionJob::extract(*source, 0, 2, bytes, n, u, s).failed(),
            "Invalid BPM accepted");
        check(ChordExtractionJob::extract(*source, 120, 3, bytes, n, u, s)
                  .failed(),
              "Invalid step accepted");
        check(ChordExtractionJob::extract(*source, 120, 2, bytes, n, u, s,
                                          [] { return true; })
                  .failed(),
              "Cancellation ignored");
      });
  test(
      "processor retains original, automatically builds and switches chord "
      "preview",
      [&] {
        DuomiSunoAIBridgeAudioProcessor p;
        p.prepareToPlay(48000, 128);
        const auto source = fixture();
        p.setPreviewMidi(source);
        settle(p);
        auto chords = p.getChordJob().getSnapshot();
        check(chords.midi && chords.chordCount == 4 &&
                  chords.midi->file.existsAsFile(),
              "Automatic chord file missing");
        p.selectPreviewMode(3);
        check(
            p.getPreviewMidi() == chords.midi && p.getOriginalMidi() == source,
            "Preview selection replaced source");
        p.selectPreviewMode(1);
        check(p.getPreviewMidi() == source, "Cannot return to original");
        p.setChordBeats(1);
        settle(p);
        check(p.getChordJob().getSnapshot().chordCount == 4 &&
                  p.getChordJob().getSnapshot().midi != chords.midi,
              "Step did not rebuild chords");
      });
  const auto analyse = [](const std::vector<std::vector<int>>& windows,
                          int& count, int& uncertain, int& skipped) {
    GeneratedMidi source, output;
    source.durationSeconds = (double)windows.size();
    for (int i = 0; i < (int)windows.size(); ++i)
      for (int pitch : windows[(size_t)i]) {
        auto on = juce::MidiMessage::noteOn(1, pitch, 0.8f);
        auto off = juce::MidiMessage::noteOff(1, pitch);
        on.setTimeStamp(i);
        off.setTimeStamp(i + 1);
        source.sequence.addEvent(on);
        source.sequence.addEvent(off);
      }
    source.sequence.updateMatchedPairs();
    juce::MemoryBlock bytes;
    check(ChordExtractionJob::extract(source, 120, 2, bytes, count, uncertain,
                                      skipped)
              .wasOk(),
          "Context fixture failed");
    check(DuomiClient::parseMidi(bytes, output).wasOk(),
          "Context output invalid");
    return output;
  };
  test("identical neighbouring chords become one sustained set of notes", [&] {
    int n, u, s;
    const auto result =
        analyse({{60, 64, 67}, {60, 64, 67}, {60, 64, 67}}, n, u, s);
    check(n == 1 && result.noteCount == 3 && s == 0,
          "Repeated chords not merged");
    for (const auto* e : result.sequence)
      if (e->message.isNoteOn())
        check(
            e->message.getTimeStamp() == 0 && e->noteOffObject &&
                std::abs(e->noteOffObject->message.getTimeStamp() - 3) < 0.001,
            "Merged chord still retriggers or ends early");
  });
  test("neighbour evidence repairs a short missing-tone window", [&] {
    for (const auto& middle : std::vector<std::vector<int>>{{60, 67}, {60}}) {
      int n, u, s;
      const auto result =
          analyse({{60, 64, 67}, middle, {60, 64, 67}}, n, u, s);
      check(
          n == 1 && u == 1 && s == 0 && result.noteCount == 3 &&
              std::abs(result.durationSeconds - 3) < 0.001,
          "Missing tone interrupted chord or repair was not marked uncertain");
    }
  });
  test("genuine silence separates identical chords", [&] {
    int n, u, s;
    const auto result = analyse({{60, 64, 67}, {}, {60, 64, 67}}, n, u, s);
    check(n == 2 && s == 0 && result.noteCount == 6,
          "Rest bridged by chord merge");
    for (const auto* e : result.sequence)
      if (e->message.isNoteOn())
        check(e->message.getTimeStamp() != 1 &&
                  e->noteOffObject->message.getTimeStamp() -
                          e->message.getTimeStamp() <=
                      1.001,
              "Chord spans true rest");
  });
  test("clear roots and chord qualities survive temporal smoothing", [&] {
    int n, u, s;
    const auto result = analyse({{60, 64, 67},
                                 {60, 64, 67, 70},
                                 {60, 64, 67},
                                 {57, 60, 64},
                                 {53, 57, 60}},
                                n, u, s);
    check(n == 5 && result.noteCount == 16 && s == 0,
          "Real change over-smoothed");
    std::vector<int> counts(5);
    for (const auto* e : result.sequence)
      if (e->message.isNoteOn())
        ++counts[(size_t)juce::roundToInt(e->message.getTimeStamp())];
    check(counts == std::vector<int>({3, 4, 3, 3, 3}),
          "Seventh chord quality lost");
    // Brief D/E competition gives Csus2 a small local advantage over C.
    // Temporal evidence should keep the C chord shared by both neighbours.
    GeneratedMidi source, stable;
    source.durationSeconds = 3;
    for (int i = 0; i < 3; ++i)
      for (int pitch : std::vector<int>{60, 62, 64, 67}) {
        if (pitch == 62 && i != 1) continue;
        auto on = juce::MidiMessage::noteOn(1, pitch, 0.8f);
        auto off = juce::MidiMessage::noteOff(1, pitch);
        on.setTimeStamp(i);
        off.setTimeStamp(i + ((pitch == 64 && i == 1) ? 0.8 : 1.0));
        source.sequence.addEvent(on);
        source.sequence.addEvent(off);
      }
    source.sequence.updateMatchedPairs();
    juce::MemoryBlock bytes;
    check(ChordExtractionJob::extract(source, 120, 2, bytes, n, u, s).wasOk() &&
              DuomiClient::parseMidi(bytes, stable).wasOk(),
          "Ambiguous context extraction failed");
    check(n == 1 && u == 1 && stable.noteCount == 3,
          "Ambiguous window caused chord flicker");
  });
  test("long weak melody is not filled and cancellation reaches analysis", [&] {
    int n, u, s;
    const auto result =
        analyse({{60, 64, 67}, {60}, {60}, {60}, {60, 64, 67}}, n, u, s);
    check(n == 2 && s == 3 && result.noteCount == 6,
          "Long melody invented harmony");
    auto source = fixture();
    juce::MemoryBlock bytes;
    int calls = 0;
    const int limit = source->sequence.getNumEvents() + 5;
    check(ChordExtractionJob::extract(
              *source, 120, 2, bytes, n, u, s,
              [&] {
                return ++calls > limit;
              }).failed() &&
              bytes.getSize() == 0 && calls > limit,
          "Cancellation ignored in temporal analysis");
  });
  test("bar rulers follow BPM with aligned pages and sparse long-song labels",
       [&] {
         const auto ticks = MusicalGrid::ticks(0, 8, 120, 592);
         check(ticks.size() == 17 && ticks[0].label == "1.1" &&
                   ticks[4].label == "2.1" && ticks[4].bar && !ticks[1].bar &&
                   ticks[16].label == "5.1",
               "4/4 bar or beat positions wrong");
         check(MusicalGrid::barSeconds(60) == 4 &&
                   MusicalGrid::pageStart(7.99, 120) == 0 &&
                   MusicalGrid::pageStart(8, 120) == 8 &&
                   MusicalGrid::pageStart(8, 60) == 0,
               "BPM or four-bar page alignment wrong");
         const auto next = MusicalGrid::ticks(8, 16, 120, 592);
         check(next.front().label == "5.1", "Page restarted bar numbering");
         const auto song = MusicalGrid::ticks(0, 187.584, 120, 584);
         double last = -100;
         int labels = 0;
         for (const auto& tick : song)
           if (tick.label.isNotEmpty()) {
             const double pixel = tick.seconds / 187.584 * 584;
             check(pixel - last >= 70, "Long-song labels overlap");
             last = pixel;
             ++labels;
           }
         check(labels >= 3 && song.size() < 100, "Long-song grid not thinned");
         check(MusicalGrid::ticks(0, 0, 120, 500).empty(),
               "Empty duration accepted");
       });
  std::cout << passed << " chord groups passed\n";
  return 0;
}

int runSessionTests() {
  int passed = 0;
  const auto test = [&](const char* name, auto fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << std::endl;
  };
  test("round trip embeds original MIDI, settings and mode without source file",
       [&] {
         Folder folder;
         juce::MemoryBlock bytes;
         auto original = fixture();
         original->file = folder.file.getChildFile("original.mid");
         check(original->file.replaceWithData(original->fileBytes.getData(),
                                              original->fileBytes.getSize()),
               "Source write failed");
         {
           DuomiSunoAIBridgeAudioProcessor p;
           p.setDescription(juce::String::fromUTF8("温暖的钢琴和弦"));
           p.setGenerationBpm(132);
           p.setChordBeats(4);
           p.setPreviewMidi(original);
           p.selectPreviewMode(3);
           p.getStateInformation(bytes);
         }
         original->file.deleteFile();
         const auto saved = decode(bytes);
         check(saved.midiBytes == original->fileBytes && saved.bpm == 132 &&
                   saved.mode == 3 && saved.chordBeats == 4,
               "Settings/bytes not persisted");
         check(!juce::String::fromUTF8((const char*)bytes.getData(),
                                       (int)bytes.getSize())
                    .contains("apiKey"),
               "API key field in session state");
         DuomiSunoAIBridgeAudioProcessor restored;
         restored.prepareToPlay(48000, 128);
         std::thread host([&] {
           restored.setStateInformation(bytes.getData(), (int)bytes.getSize());
         });
         host.join();
         juce::MemoryBlock immediate;
         restored.getStateInformation(immediate);
         check(immediate == bytes, "Saving before async restore lost state");
         settle(restored);
         check(
             restored.getOriginalMidi() &&
                 restored.getOriginalMidi()->fileBytes == original->fileBytes &&
                 restored.getOriginalMidi()->file.existsAsFile(),
             "Embedded MIDI not restored");
         check(restored.getDescription() == saved.prompt &&
                   restored.getGenerationBpm() == 132 &&
                   restored.getChordBeats() == 4 &&
                   restored.getPreviewMode() == 3 && restored.getPreviewMidi(),
               "Settings/chord mode did not recover");
         renderStopped(restored);
         std::unique_ptr<juce::AudioProcessorEditor> editor(
             restored.createEditor());
         bool descriptionFound = false, bpmFound = false;
         for (auto* child : editor->getChildren()) {
           if (auto* input = dynamic_cast<juce::TextEditor*>(child))
             descriptionFound |= input->getText() == saved.prompt;
           if (auto* bpm = dynamic_cast<cyber::BpmControl*>(child))
             bpmFound = bpm->getBpm() == 132;
         }
         check(descriptionFound && bpmFound,
               "Editor did not display restored settings");
       });
  test(
      "invalid, oversized, future-version and empty legacy states leave values "
      "unchanged",
      [&] {
        DuomiSunoAIBridgeAudioProcessor p;
        p.setGenerationBpm(154);
        p.setDescription("keep me");
        const char* bad = "{\"type\":\"DuomiSunoAIBridge\",\"version\":99}";
        p.setStateInformation(bad, (int)std::strlen(bad));
        p.setStateInformation(nullptr, 0);
        p.setStateInformation(bad, 25 * 1024 * 1024);
        check(!p.isRestoringSession() && p.getGenerationBpm() == 154 &&
                  p.getDescription() == "keep me",
              "Bad state changed settings");
        SessionState state;
        state.audioFile = "../relative.wav";
        auto bytes = state.encode();
        p.setStateInformation(bytes.getData(), (int)bytes.getSize());
        check(!p.isRestoringSession(), "Relative audio path accepted");
      });
  test(
      "task recovery never calls transport and remains resumable with old "
      "preview",
      [&] {
        int requests = 0;
        GenerationJob job({}, [&](const auto&, auto&) {
          ++requests;
          return HttpResponse{};
        });
        GenerationJob::Snapshot meta;
        meta.taskId = "saved-song";
        meta.midiTaskId = "saved-extraction";
        meta.audioUrl = "https://audio.invalid/old.mp3";
        meta.resumeAvailable = true;
        auto original = fixture();
        check(job.restore(meta, original->fileBytes, "old.mid"),
              "Task restore failed");
        for (int i = 0; i < 1600 && job.getSnapshot().busy; ++i)
          juce::Thread::sleep(5);
        const auto status = job.getSnapshot();
        check(!status.busy && !status.failed && status.midi &&
                  status.resumeAvailable && status.taskId == meta.taskId &&
                  status.midiTaskId == meta.midiTaskId && requests == 0,
              "Restore submitted a request or lost resumable task");
      });
  test(
      "latest queued state wins; restoring empty state clears prior MIDI and "
      "audio",
      [&] {
        DuomiSunoAIBridgeAudioProcessor p;
        p.prepareToPlay(48000, 128);
        p.setPreviewMidi(fixture());
        settle(p);
        SessionState first, second;
        first.prompt = "obsolete";
        first.bpm = 95;
        first.midiBytes = fixture()->fileBytes;
        second.prompt = "latest";
        second.bpm = 175;
        auto a = first.encode(), b = second.encode();
        p.setStateInformation(a.getData(), (int)a.getSize());
        p.refreshBackgroundState();
        p.setStateInformation(b.getData(), (int)b.getSize());
        settle(p);
        check(p.getDescription() == "latest" && p.getGenerationBpm() == 175 &&
                  !p.getOriginalMidi() && !p.getChordJob().getSnapshot().midi &&
                  !p.getAudioPreviewPlayer().getSnapshot().ready,
              "Old restoration won or stale preview retained");
        renderStopped(p);
      });
  test(
      "missing audio reports error and preserves reference without losing MIDI",
      [&] {
        Folder folder;
        SessionState state;
        state.mode = 2;
        state.midiBytes = fixture()->fileBytes;
        state.audioFile =
            folder.file.getChildFile("missing.wav").getFullPathName();
        auto bytes = state.encode();
        DuomiSunoAIBridgeAudioProcessor p;
        p.prepareToPlay(48000, 128);
        p.setStateInformation(bytes.getData(), (int)bytes.getSize());
        settle(p);
        check(p.getOriginalMidi() &&
                  p.getAudioPreviewPlayer().getSnapshot().failed &&
                  !p.getAudioPreviewPlayer().getSnapshot().ready,
              "Missing audio not reported");
        p.getStateInformation(bytes);
        check(decode(bytes).audioFile == state.audioFile,
              "Missing file reference lost on resave");
        renderStopped(p);
      });
  test("local WAV restores independently and remains stopped", [&] {
    Folder folder;
    const auto audioFile = folder.file.getChildFile("audio.wav");
    juce::WavAudioFormat format;
    auto stream = audioFile.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), 24000, 1, 16, {}, 0));
    check(writer != nullptr, "WAV writer failed");
    stream.release();
    juce::AudioBuffer<float> audio(1, 12000);
    for (int i = 0; i < 12000; ++i)
      audio.setSample(0, i, 0.2f * std::sin(i * 0.07f));
    check(writer->writeFromAudioSampleBuffer(audio, 0, 12000),
          "WAV write failed");
    writer.reset();
    SessionState state;
    state.mode = 2;
    state.audioFile = audioFile.getFullPathName();
    auto bytes = state.encode();
    DuomiSunoAIBridgeAudioProcessor p;
    p.prepareToPlay(48000, 128);
    p.setStateInformation(bytes.getData(), (int)bytes.getSize());
    settle(p);
    check(p.getAudioPreviewPlayer().getSnapshot().ready &&
              p.isAudioPreviewSelected(),
          "Local WAV not restored");
    renderStopped(p);
    p.playPreview();
    juce::AudioBuffer<float> out(2, 512);
    juce::MidiBuffer events;
    p.processBlock(out, events);
    check(out.getMagnitude(0, 512) > 0, "Restored WAV not playable");
    SessionState empty;
    bytes = empty.encode();
    p.setStateInformation(bytes.getData(), (int)bytes.getSize());
    settle(p);
    renderStopped(p);
    check(!p.getAudioPreviewPlayer().getSnapshot().ready,
          "Old WAV remains ready");
  });
  test("host is notified of edits but idle polling does not dirty the project",
       [&] {
         struct Listener : juce::AudioProcessorListener {
           int changes = 0;
           void audioProcessorParameterChanged(juce::AudioProcessor*, int,
                                               float) override {}
           void audioProcessorChanged(juce::AudioProcessor*,
                                      const ChangeDetails& details) override {
             if (details.nonParameterStateChanged) ++changes;
           }
         } listener;
         DuomiSunoAIBridgeAudioProcessor p;
         p.addListener(&listener);
         p.setDescription("new");
         p.setGenerationBpm(140);
         p.setChordBeats(4);
         p.selectPreviewMode(3);
         check(listener.changes == 4,
               "User edits did not mark host state changed");
         for (int i = 0; i < 10; ++i) p.refreshBackgroundState();
         p.setDescription("new");
         p.setGenerationBpm(140);
         p.setChordBeats(4);
         check(listener.changes == 4,
               "Idle polling or unchanged values dirty project");
         p.removeListener(&listener);
       });
  std::cout << passed << " session groups passed\n";
  return 0;
}

int chordProbe(const juce::File& source, const juce::File& output) {
  juce::MemoryBlock original, chords;
  check(source.loadFileAsData(original), "Cannot read real MIDI");
  GeneratedMidi midi;
  check(DuomiClient::parseMidi(original, midi).wasOk(),
        "Cannot parse real MIDI");
  int count, uncertain, skipped;
  auto result = ChordExtractionJob::extract(midi, 120, 2, chords, count,
                                            uncertain, skipped);
  check(result.wasOk(), result.getErrorMessage().toRawUTF8());
  check(output.replaceWithData(chords.getData(), chords.getSize()),
        "Cannot write chord file");
  DuomiSunoAIBridgeAudioProcessor p;
  p.prepareToPlay(48000, 512);
  auto clip = std::make_shared<GeneratedMidi>(midi);
  clip->file = source;
  p.setPreviewMidi(clip);
  settle(p);
  p.selectPreviewMode(3);
  p.setDescription(juce::String::fromUTF8("钢琴柱式和弦 · 保留原始 MIDI"));
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  auto png = output.withFileExtension(".png").createOutputStream();
  check(png != nullptr, "PNG stream failed");
  png->setPosition(0);
  png->truncate();
  check(juce::PNGImageFormat().writeImageToStream(
            editor->createComponentSnapshot(editor->getLocalBounds()), *png),
        "Chord screenshot failed");
  std::cout << "Chords=" << count << " uncertain=" << uncertain
            << " skipped=" << skipped << " output=" << output.getFullPathName()
            << std::endl;
  return 0;
}