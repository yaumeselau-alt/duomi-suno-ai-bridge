#include <iostream>
#include <stdexcept>

#include "PluginEditor.h"
using namespace duomi;
namespace {
void expect(bool value, const char* msg) {
  if (!value) throw std::runtime_error(msg);
}
std::shared_ptr<GeneratedMidi> tracks(bool mixed = false, bool smpte = false) {
  juce::MidiFile midi;
  if (smpte)
    midi.setSmpteTimeFormat(25, 40);
  else
    midi.setTicksPerQuarterNote(960);
  juce::MidiMessageSequence global, a, b;
  global.addEvent(juce::MidiMessage::tempoMetaEvent(500000));
  auto tempo = juce::MidiMessage::tempoMetaEvent(1000000);
  tempo.setTimeStamp(960);
  global.addEvent(tempo);
  a.addEvent(juce::MidiMessage::textMetaEvent(3, "Piano"));
  b.addEvent(juce::MidiMessage::textMetaEvent(3, "Bass"));
  for (auto pair : std::vector<std::pair<int, int>>{{1, 60}, {2, 36}}) {
    auto& sequence = pair.first == 1 ? a : b;
    sequence.addEvent(
        juce::MidiMessage::programChange(pair.first, pair.first == 1 ? 0 : 32));
    auto cc = juce::MidiMessage::controllerEvent(pair.first, 64, 127);
    cc.setTimeStamp(900);
    sequence.addEvent(cc);
    auto on = juce::MidiMessage::noteOn(pair.first, pair.second, 0.8f);
    auto off = juce::MidiMessage::noteOff(pair.first, pair.second);
    on.setTimeStamp(960);
    off.setTimeStamp(1920);
    sequence.addEvent(on);
    sequence.addEvent(off);
  }
  if (mixed) {
    a.addSequence(b, 0);
    a.addSequence(global, 0);
    midi.addTrack(a);
  } else {
    midi.addTrack(global);
    midi.addTrack(a);
    midi.addTrack(b);
  }
  juce::MemoryOutputStream stream;
  expect(midi.writeTo(stream, mixed ? 0 : 1), "fixture write");
  auto result = std::make_shared<GeneratedMidi>();
  expect(DuomiClient::parseMidi(stream.getMemoryBlock(), *result).wasOk(),
         "fixture parse");
  return result;
}
void settle(DuomiSunoAIBridgeAudioProcessor& p) {
  for (int i = 0; i < 1600; ++i) {
    p.refreshBackgroundState();
    if (!p.isRestoringSession() && !p.getMidiTrackJob().getSnapshot().busy &&
        !p.getChordJob().getSnapshot().busy &&
        !p.getGenerationJob().getSnapshot().busy &&
        !p.getAudioPreviewPlayer().getSnapshot().busy)
      return;
    juce::Thread::sleep(5);
  }
  throw std::runtime_error("Track settle timeout");
}
HttpResponse json(juce::String s, int status = 200) {
  HttpResponse r;
  r.status = status;
  r.contentType = "application/json";
  r.body.append(s.toRawUTF8(), s.getNumBytesAsUTF8());
  return r;
}
ClientOptions opts() {
  ClientOptions o;
  o.pollIntervalMs = 0;
  o.maxPolls = 1;
  return o;
}
juce::ComboBox* choice(juce::AudioProcessorEditor& editor) {
  for (auto* c : editor.getChildren())
    if (c->getName() == juce::String::fromUTF8("轨道选择"))
      return dynamic_cast<juce::ComboBox*>(c);
  return nullptr;
}
void snapshot(juce::AudioProcessorEditor& editor, const char* name) {
  auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(
      "build/live-validation");
  dir.createDirectory();
  auto out = dir.getChildFile(name).createOutputStream();
  expect(out != nullptr, "screenshot stream");
  out->setPosition(0);
  out->truncate();
  expect(juce::PNGImageFormat().writeImageToStream(
             editor.createComponentSnapshot(editor.getLocalBounds()), *out),
         "screenshot");
}
}  // namespace
int runTrackSelectionTests() {
  int passed = 0;
  const auto test = [&](const char* name, auto fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << std::endl;
  };
  test("MIDI tracks and mixed channels preserve tempo, onset and controllers",
       [] {
         for (bool mixed : {false, true})
           for (bool smpte : {false, true}) {
             auto source = tracks(mixed, smpte);
             expect(source->parts.size() == 2, "track discovery");
             const auto original = source->fileBytes;
             for (int i = 1; i <= 2; ++i) {
               juce::MemoryBlock bytes;
               GeneratedMidi out;
               expect(MidiTrackJob::extract(*source, i, bytes).wasOk() &&
                          DuomiClient::parseMidi(bytes, out).wasOk(),
                      "extract");
               expect(out.noteCount == 1 && out.parts.size() == 1,
                      "leaked other track");
               bool controller = false, program = false;
               for (const auto* e : out.sequence) {
                 const auto& m = e->message;
                 expect(m.getChannel() == i, "leaked other channel");
                 controller |= m.isController();
                 program |= m.isProgramChange();
                 if (m.isNoteOn())
                   expect(
                       m.getNoteNumber() == (i == 1 ? 60 : 36) &&
                           std::abs(m.getTimeStamp() - (smpte ? 0.96 : 0.5)) <
                               0.001 &&
                           std::abs(e->noteOffObject->message.getTimeStamp() -
                                    (smpte ? 1.92 : 1.5)) < 0.001,
                       "tempo or initial silence lost");
               }
               expect(controller && program && source->fileBytes == original,
                      "controls/source changed");
             }
           }
       });
  test(
      "selection switches preview and drag file; chords lock choice; session "
      "restores",
      [] {
        DuomiSunoAIBridgeAudioProcessor p;
        p.prepareToPlay(48000, 128);
        auto source = tracks();
        p.setPreviewMidi(source);
        settle(p);
        p.selectMidiTrack(1);
        p.refreshBackgroundState();
        p.selectMidiTrack(2);
        settle(p);
        expect(p.getPreviewMidi() && p.getPreviewMidi()->noteCount == 1 &&
                   p.getPreviewMidi()->file.existsAsFile() &&
                   p.getOriginalMidi() == source,
               "selection did not publish");
        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        {
          std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
          auto* select = choice(*editor);
          expect(select && select->isEnabled() && select->getNumItems() == 3,
                 "MIDI selector missing");
          expect(select->getSelectedId() == 3 && select->getX() == 176,
                 "wrong selected track or position");
          snapshot(*editor, "track-choice-midi.png");
        }
        p.selectPreviewMode(3);
        {
          std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
          expect(!choice(*editor)->isEnabled(), "chord selector editable");
          snapshot(*editor, "track-choice-chords.png");
        }
        p.selectPreviewMode(1);
        expect(p.getMidiTrack() == 2, "mode lost choice");
        DuomiSunoAIBridgeAudioProcessor restored;
        restored.setStateInformation(saved.getData(), (int)saved.getSize());
        settle(restored);
        expect(restored.getMidiTrack() == 2 && restored.getPreviewMidi() &&
                   restored.getPreviewMidi()->noteCount == 1 &&
                   restored.getOriginalMidi()->noteCount == 2,
               "selection restore");
        restored.setPreviewMidi(tracks());
        expect(restored.getMidiTrack() == 0, "new source retained old index");
      });
  test(
      "stem request uses official clip, returns tracks and never resubmits "
      "completed batch",
      [] {
        int posts = 0, queries = 0;
        DuomiClient client(opts(), [&](const HttpRequest& req, Cancellation&) {
          expect(req.headers.contains("Authorization: fake-key"),
                 "missing auth");
          if (req.method == "POST") {
            ++posts;
            auto body = juce::JSON::parse(req.url.getPostData());
            expect(body["task"].toString() == "gen_stem" &&
                       body["mv"].toString() == "chirp-auk" &&
                       body["stem_task"].toString() == "twelve" &&
                       body["continue_clip_id"].toString() == "source-clip",
                   "stem body");
            return json(
                R"({"code":200,"data":[{"task_id":"piano"},{"task_id":"drums"}]})");
          }
          ++queries;
          auto id =
              req.url.getParameterValues()[req.url.getParameterNames().indexOf(
                  "task_id")];
          if (id == "source")
            return json(
                R"({"code":200,"data":{"state":"succeeded","clip_id":"source-clip"}})");
          return json(
              "{\"code\":200,\"data\":{\"state\":\"succeeded\",\"title\":\"" +
              id + "\",\"audio_url\":\"https://audio.invalid/" + id +
              ".wav\"}}");
        });
        Cancellation token;
        StemBatch b;
        b.sourceTask = "source";
        expect(client.separateStems("fake-key", token, b).wasOk() &&
                   b.stems.size() == 2 && posts == 1 && queries == 3,
               "stem pipeline");
        expect(client.separateStems("fake-key", token, b).wasOk() &&
                   posts == 1 && queries == 3,
               "completed batch re-requested");
        expect(b.stems[0].name == "piano" && b.stems[1].name.contains("drums"),
               "stem names");
      });
  test("timeout and cancellation retain task IDs for query-only resume", [] {
    int posts = 0, phase = 0;
    DuomiClient client(opts(), [&](const HttpRequest& req, Cancellation&) {
      if (req.method == "POST") {
        ++posts;
        return json(R"({"code":200,"data":[{"task_id":"stem"}]})");
      }
      if (req.url.getParameterValues().contains("source"))
        return json(R"({"data":{"state":"succeeded","clip_id":"clip"}})");
      if (!phase) return json(R"({"data":{"state":"running"}})");
      return json(
          R"({"data":{"state":"succeeded","title":"Piano","audio_url":"https://audio.invalid/piano.wav"}})");
    });
    Cancellation c;
    StemBatch b;
    b.sourceTask = "source";
    expect(client.separateStems("fake", c, b).failed() && b.stems.size() == 1 &&
               posts == 1,
           "timeout lost tasks");
    phase = 1;
    expect(client.separateStems("fake", c, b).wasOk() && posts == 1,
           "resume duplicated charge");
    Cancellation cancelled;
    cancelled.cancel();
    StemBatch fresh;
    fresh.sourceTask = "source";
    expect(
        client.separateStems("fake", cancelled, fresh).failed() && posts == 1,
        "cancel submitted task");
  });
  test(
      "ambiguous billable failure blocks duplicate submission and rejects "
      "missing key",
      [] {
        int posts = 0;
        DuomiClient client(opts(), [&](const HttpRequest& req, Cancellation&) {
          if (req.method == "POST") {
            ++posts;
            return json(R"({"msg":"server failed"})", 500);
          }
          return json(R"({"data":{"state":"succeeded","clip_id":"clip"}})");
        });
        Cancellation c;
        StemBatch b;
        b.sourceTask = "source";
        expect(client.separateStems("fake", c, b).failed() &&
                   b.submissionUncertain,
               "uncertain submission not retained");
        expect(client.separateStems("fake", c, b).failed() && posts == 1,
               "unsafe POST retry");
        expect(client.separateStems("", c, b).failed(), "empty key accepted");
      });
  test(
      "audio selection restores local stems, keeps full mix and updates "
      "waveform/drag",
      [] {
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("duomi-track-" + juce::Uuid().toString());
        expect(dir.createDirectory().wasOk(), "audio folder");
        auto create = [&](const char* name, float value) {
          auto file = dir.getChildFile(name);
          juce::WavAudioFormat format;
          auto out = file.createOutputStream();
          std::unique_ptr<juce::AudioFormatWriter> w(
              format.createWriterFor(out.get(), 24000, 1, 16, {}, 0));
          expect(w != nullptr, "audio writer");
          out.release();
          juce::AudioBuffer<float> data(1, 2400);
          for (int i = 0; i < 2400; ++i) data.setSample(0, i, value);
          expect(w->writeFromAudioSampleBuffer(data, 0, 2400), "audio write");
          return file;
        };
        const auto mix = create("mix.wav", 0.1f),
                   piano = create("piano.wav", 0.3f);
        SessionState saved;
        saved.mode = 2;
        saved.audioTrack = 1;
        saved.audioFile = mix.getFullPathName();
        saved.audioSourceTask = "source";
        saved.stemBatch.sourceTask = "source";
        saved.stemBatch.stems.push_back({"piano", "Piano",
                                         "https://audio.invalid/piano.wav",
                                         piano.getFullPathName()});
        const auto bytes = saved.encode();
        {
          DuomiSunoAIBridgeAudioProcessor p;
          p.prepareToPlay(48000, 128);
          p.setStateInformation(bytes.getData(), (int)bytes.getSize());
          settle(p);
          expect(p.getAudioTrack() == 1 &&
                     p.getAudioPreviewPlayer().getSnapshot().ready,
                 "local stem not restored");
          juce::MemoryBlock resaved;
          p.getStateInformation(resaved);
          SessionState state;
          expect(SessionState::decode(resaved.getData(), (int)resaved.getSize(),
                                      state) &&
                     state.audioFile != state.stemBatch.stems[0].file,
                 "full mix overwritten by stem");
          {
            std::unique_ptr<juce::AudioProcessorEditor> editor(
                p.createEditor());
            expect(choice(*editor)->isEnabled() &&
                       choice(*editor)->getSelectedId() == 2,
                   "audio choice mismatch");
            snapshot(*editor, "track-choice-audio.png");
          }
          p.playPreview();
          juce::AudioBuffer<float> audio(2, 128);
          juce::MidiBuffer events;
          for (int i = 0; i < 4; ++i) p.processBlock(audio, events);
          expect(audio.getMagnitude(0, 128) > 0.2f, "wrong stem audio");
          const auto stemFile =
              p.getAudioPreviewPlayer().getSnapshot().dragFile;
          expect(p.selectAudioTrack(0), "mix select");
          p.playPreview();
          for (int i = 0; i < 4; ++i) p.processBlock(audio, events);
          expect(
              audio.getMagnitude(0, 128) < 0.15f &&
                  audio.getMagnitude(0, 128) > 0.05f &&
                  p.getAudioPreviewPlayer().getSnapshot().dragFile != stemFile,
              "mix not retained");
          expect(p.selectAudioTrack(1), "cached stem select");
          settle(p);
          expect(p.getAudioPreviewPlayer().getSnapshot().ready,
                 "cached stem failed");
        }
        dir.deleteRecursively();
      });
  test(
      "legacy states default to all tracks and malformed stem references are "
      "rejected",
      [] {
        SessionState s;
        auto bytes = s.encode();
        auto value = juce::JSON::parse(juce::String::fromUTF8(
            (const char*)bytes.getData(), (int)bytes.getSize()));
        for (auto field :
             {"midiTrack", "audioTrack", "audioSourceTask", "stems"})
          value.getDynamicObject()->removeProperty(field);
        auto old = juce::JSON::toString(value);
        SessionState decoded;
        expect(SessionState::decode(old.toRawUTF8(),
                                    (int)old.getNumBytesAsUTF8(), decoded) &&
                   decoded.midiTrack == 0 && decoded.audioTrack == 0,
               "legacy incompatible");
        s.audioSourceTask = "source";
        s.stemBatch.sourceTask = "source";
        s.stemBatch.stems.push_back(
            {"x", "x", "https://audio.invalid/x", "../x.wav"});
        bytes = s.encode();
        expect(!SessionState::decode(bytes.getData(), (int)bytes.getSize(),
                                     decoded),
               "relative stem path accepted");
      });
  test(
      "one parent result expands instruments, deduplicates variants and avoids "
      "encrypted URLs",
      [] {
        juce::Array<juce::var> clips;
        for (int version = 0; version < 2; ++version)
          for (auto name : {"Keyboard", "Bass", "Drums"}) {
            juce::DynamicObject::Ptr item = new juce::DynamicObject,
                                     meta = new juce::DynamicObject;
            meta->setProperty("stem_type_group_name", name);
            item->setProperty("metadata", juce::var(meta.get()));
            item->setProperty("status", "complete");
            item->setProperty("audio_url", "http://cn2.m4a.bmnmny.cn/" +
                                               juce::String(name) +
                                               juce::String(version) + ".m4a");
            clips.add(juce::var(item.get()));
          }
        juce::DynamicObject::Ptr result = new juce::DynamicObject,
                                 root = new juce::DynamicObject;
        result->setProperty("state", "succeeded");
        result->setProperty("audio_url",
                            "https://studio-api.prod.suno.com/api/forbidden");
        result->setProperty("data", clips);
        root->setProperty("data", juce::var(result.get()));
        int requests = 0;
        DuomiClient client(opts(), [&](const HttpRequest& req, Cancellation&) {
          expect(req.method == "GET", "resume submitted generation");
          ++requests;
          return json(juce::JSON::toString(juce::var(root.get())));
        });
        StemBatch b;
        b.sourceTask = "song";
        b.stems.push_back({"parent", "pending", {}, {}});
        Cancellation c;
        expect(client.separateStems("fake", c, b).wasOk() && requests == 1 &&
                   b.stems.size() == 3,
               "parent batch not expanded");
        expect(
            b.stems[0].name.contains("Keyboard") &&
                b.stems[0].url == "https://cn2.m4a.bmnmny.cn/Keyboard0.m4a" &&
                b.stems[1].url.endsWith("Bass0.m4a"),
            "wrong variant or media URL");
      });
  test(
      "persistent stem cache reopens without network and protects unknown "
      "submissions",
      [] {
        auto dir =
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("duomi-stem-cache-" + juce::Uuid().toString());
        int requests = 0;
        const auto transport = [&](const HttpRequest&, Cancellation&) {
          ++requests;
          return json(
              R"({"data":{"state":"succeeded","title":"Keyboard","audio_url":"https://audio.invalid/keys.wav"}})");
        };
        const auto wait = [](AudioStemJob& job) {
          for (int i = 0; i < 1000 && job.getSnapshot().busy; ++i)
            juce::Thread::sleep(5);
          expect(!job.getSnapshot().busy, "cache worker timed out");
        };
        {
          AudioStemJob job(opts(), transport, dir);
          StemBatch batch;
          batch.sourceTask = "source";
          batch.stems.push_back({"existing", "pending", {}, {}});
          job.restore(batch);
          expect(job.start("fake", "source"), "cache start");
          wait(job);
          expect(!job.getSnapshot().failed && requests == 1,
                 "initial query failed");
        }
        {
          AudioStemJob reopened(opts(), transport, dir);
          expect(reopened.start("fake", "source"), "reopen start");
          wait(reopened);
          expect(!reopened.getSnapshot().failed &&
                     reopened.getSnapshot().batch.stems.size() == 1 &&
                     requests == 1,
                 "reopen resubmitted or queried");
        }
        SessionState unknown;
        unknown.audioSourceTask = "unknown";
        unknown.stemBatch.sourceTask = "unknown";
        unknown.stemBatch.submissionUncertain = true;
        auto bytes = unknown.encode();
        dir.getChildFile(AudioStemJob::cacheName("unknown"))
            .replaceWithData(bytes.getData(), bytes.getSize());
        {
          AudioStemJob blocked(opts(), transport, dir);
          expect(blocked.start("fake", "unknown"), "unknown start");
          wait(blocked);
          expect(blocked.getSnapshot().failed && requests == 1,
                 "uncertain cache sent request");
        }
        dir.deleteRecursively();
      });
  std::cout << passed << " track groups passed\n";
  return 0;
}

int stemLiveProbe(const juce::String& sourceTask) {
  ApiKeyStore keys;
  for (int i = 0; i < 1000 && keys.getSnapshot().busy; ++i)
    juce::Thread::sleep(5);
  const auto key = keys.getSnapshot().key;
  expect(key.isNotEmpty(), "No stored key");
  auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(
      "build/live-validation");
  dir.createDirectory();
  auto stateFile = dir.getChildFile("stem-probe-state.json");
  SessionState saved;
  saved.audioSourceTask = sourceTask;
  saved.stemBatch.sourceTask = sourceTask;
  if (stateFile.existsAsFile()) {
    juce::MemoryBlock b;
    stateFile.loadFileAsData(b);
    SessionState old;
    if (SessionState::decode(b.getData(), (int)b.getSize(), old) &&
        old.audioSourceTask == sourceTask)
      saved = old;
  }
  ClientOptions options;
  options.logFile = dir.getChildFile("stem-probe.log");
  Cancellation c;
  const auto persist = [&] {
    auto bytes = saved.encode();
    expect(stateFile.replaceWithData(bytes.getData(), bytes.getSize()),
           "Cannot persist stem tasks");
  };
  auto result = DuomiClient(options).separateStems(
      key, c, saved.stemBatch, [&](const juce::String& msg) {
        persist();
        std::cout << msg << std::endl;
      });
  persist();
  expect(result.wasOk(), result.getErrorMessage().toRawUTF8());
  std::cout << "Stem tracks=" << saved.stemBatch.stems.size() << std::endl;
  for (const auto& stem : saved.stemBatch.stems)
    std::cout << stem.name << " task=" << stem.taskId << std::endl;
  return 0;
}
int stemShowcase() {
  auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(
      "build/live-validation");
  juce::MemoryBlock bytes;
  SessionState saved;
  expect(dir.getChildFile("stem-probe-state.json").loadFileAsData(bytes) &&
             SessionState::decode(bytes.getData(), (int)bytes.getSize(), saved),
         "No real stem result");
  saved.audioFile =
      dir.getChildFile("C Major Drift-audio-preview.wav").getFullPathName();
  saved.taskId = saved.audioSourceTask;
  saved.audioUrl = juce::SystemStats::getEnvironmentVariable(
      "DUOMI_SHOWCASE_AUDIO_URL", {});
  dir.getChildFile("C Major Drift.mid").loadFileAsData(saved.midiBytes);
  saved.midiName = "C Major Drift.mid";
  saved.mode = 2;
  int index = 0;
  for (int i = 0; i < (int)saved.stemBatch.stems.size(); ++i)
    if (saved.stemBatch.stems[(size_t)i].name.contains("Keyboard"))
      index = i + 1;
  expect(index > 0, "Keyboard missing");
  DuomiSunoAIBridgeAudioProcessor p;
  p.prepareToPlay(48000, 512);
  bytes = saved.encode();
  p.setStateInformation(bytes.getData(), (int)bytes.getSize());
  settle(p);
  expect(p.selectAudioTrack(index), "Real keyboard selection failed");
  for (int i = 0; i < 3000 && p.getAudioPreviewPlayer().getSnapshot().busy;
       ++i) {
    p.refreshBackgroundState();
    juce::Thread::sleep(20);
  }
  p.refreshBackgroundState();
  const auto audio = p.getAudioPreviewPlayer().getSnapshot();
  expect(audio.ready && !audio.failed && audio.dragFile.existsAsFile(),
         "Real keyboard decode/drag failed");
  p.getStateInformation(bytes);
  expect(SessionState::decode(bytes.getData(), (int)bytes.getSize(), saved),
         "Showcase state failed");
  saved.stemBatch = p.getAudioStemJob().getSnapshot().batch;
  const auto cache = AudioStemJob::defaultCacheDirectory().getChildFile(
      AudioStemJob::cacheName(saved.audioSourceTask));
  expect(cache.getParentDirectory().createDirectory().wasOk(),
         "Real cache folder failed");
  bytes = saved.encode();
  expect(cache.replaceWithData(bytes.getData(), bytes.getSize()),
         "Real cache save");
  dir.getChildFile("track-choice-real-state.json")
      .replaceWithData(bytes.getData(), bytes.getSize());
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  snapshot(*editor, "track-choice-real-keyboard.png");
  expect(choice(*editor)->getNumItems() == 13 &&
             choice(*editor)->getSelectedId() == index + 1,
         "Real selector mismatch");
  std::cout << "PASS real keyboard selection: 12 instruments, WAV="
            << audio.dragFile.getFullPathName() << std::endl;
  return 0;
}