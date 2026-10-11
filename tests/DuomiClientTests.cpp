#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "DuomiClient.h"
#include "GenerationJob.h"
#include "PluginEditor.h"

namespace {
using namespace duomi;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

HttpResponse jsonResponse(const juce::String& json, int status = 200) {
  HttpResponse result;
  result.status = status;
  result.contentType = "application/json";
  result.body.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
  return result;
}

juce::MemoryBlock fixtureMidi(bool notes = true) {
  juce::MidiMessageSequence tempo, melody;
  auto firstTempo = juce::MidiMessage::tempoMetaEvent(500000);
  tempo.addEvent(firstTempo);
  auto secondTempo = juce::MidiMessage::tempoMetaEvent(1000000);
  secondTempo.setTimeStamp(480);
  tempo.addEvent(secondTempo);
  if (notes) {
    auto on = juce::MidiMessage::noteOn(1, 60, 0.8f);
    melody.addEvent(on);
    auto off = juce::MidiMessage::noteOff(1, 60);
    off.setTimeStamp(960);  // 0.5 s at 120 BPM + 1 s at 60 BPM.
    melody.addEvent(off);
  }
  juce::MidiFile midi;
  midi.setTicksPerQuarterNote(480);
  midi.addTrack(tempo);
  midi.addTrack(melody);
  juce::MemoryOutputStream stream;
  require(midi.writeTo(stream), "Cannot construct MIDI fixture");
  return stream.getMemoryBlock();
}

HttpResponse binaryResponse() {
  HttpResponse result;
  result.status = 200;
  result.contentType = "audio/midi";
  result.body = fixtureMidi();
  return result;
}

const auto generated = [] {
  return jsonResponse(
      R"({"code":200,"data":[{"task_id":"first-task"},{"task_id":"second-task"}]})");
};
const auto pending = [] {
  return jsonResponse(R"({"code":200,"data":{"state":"pending"}})");
};
const auto succeeded = [] {
  return jsonResponse(
      R"({"code":200,"data":{"state":"succeeded","audio_url":"https://cdn.example/song.mp3"}})");
};

const juce::String midiJobId = "11111111-2222-3333-4444-555555555555";
const auto midiSubmitted = [] {
  return jsonResponse("{\"code\":200,\"msg\":\"success\",\"data\":\"" +
                      midiJobId + "\"}");
};
const auto midiCompleted = [] {
  return jsonResponse(
      R"({"code":200,"data":{"action":"midi","state":"succeeded","data":{
    "state":"complete","instruments":[
      {"name":"Piano","program":0,"is_drum":false,"notes":[
        {"pitch":60,"start":0.125,"end":1.5,"velocity":0.5},
        {"pitch":64,"start":1.5,"end":2.0,"velocity":1.0}]},
      {"name":"Drums","program":0,"is_drum":true,"notes":[
        {"pitch":36,"start":0.0,"end":0.25,"velocity":0.75}]}
    ]}}})");
};

struct Scenario {
  std::vector<HttpResponse> responses;
  std::vector<HttpRequest> requests;
  ClientOptions options;
  GeneratedMidi output;
  Cancellation cancellation;
  explicit Scenario(juce::File folder) {
    options.outputDirectory = folder;
    options.logFile = folder.getChildFile("test.log");
    options.pollIntervalMs = 0;
    options.maxPolls = 3;
  }
  juce::Result run(const juce::String& resume = {},
                   DuomiClient::Progress progress = {},
                   const juce::String& resumeMidi = {}) {
    DuomiClient client(options,
                       [this](const HttpRequest& request, Cancellation&) {
                         require(requests.size() < responses.size(),
                                 "Unexpected request (possible paid retry)");
                         requests.push_back(request);
                         return responses[requests.size() - 1];
                       });
    return client.generate("test-key", "piano melody", cancellation, output,
                           progress, resume, resumeMidi);
  }
};

int offlineTests() {
  const auto folder = juce::File::getCurrentWorkingDirectory()
                          .getChildFile("duomi-test-results")
                          .getChildFile(juce::Uuid().toString());
  int passed = 0;
  const auto test = [&](const char* name, auto fn) {
    fn();
    std::cout << "PASS " << name << std::endl;
    ++passed;
  };
  test(
      "generate -> pending -> succeeded -> binary MIDI; tempo and original "
      "file preserved",
      [&] {
        Scenario s(folder.getChildFile("binary"));
        s.responses = {generated(), pending(), succeeded(), binaryResponse()};
        auto result = s.run();
        require(result.wasOk(), result.getErrorMessage().toRawUTF8());
        require(s.output.taskId == "first-task",
                "Must choose first generated task");
        require(s.output.noteCount == 1 &&
                    std::abs(s.output.durationSeconds - 1.5) < 1e-6,
                "Tempo map or final note-off was lost");
        require(s.output.sequence.getNumEvents() == 2,
                "Expected a playable note pair");
        juce::MemoryBlock saved;
        require(s.output.file.loadFileAsData(saved) && saved == fixtureMidi(),
                "Saved MIDI differs from download");
        require(s.requests[0].method == "POST" &&
                    s.requests[0].headers.contains("Authorization: test-key"),
                "Wrong generate authentication");
        require(!s.requests[0].headers.contains("Bearer"),
                "Unexpected Bearer prefix");
        auto body = juce::JSON::parse(s.requests[0].url.getPostData());
        require(body["make_instrumental"] == juce::var(true) &&
                    body["prompt"].toString() == "piano melody",
                "Wrong generate payload");
        require(!s.requests[3].headers.contains("Authorization") &&
                    s.requests[3].url.toString(true).contains("key=test-key"),
                "Wrong MIDI authentication");
        require(
            s.requests[3].method == "GET" &&
                juce::JSON::parse(s.requests[3].url.getPostData())["task_id"]
                        .toString() == "first-task",
            "MIDI task_id must be sent in GET JSON body");
        require(!s.options.logFile.loadFileAsString().contains("test-key"),
                "Key leaked into log");
        std::cout << "Fixture MIDI: " << s.output.file.getFullPathName()
                  << std::endl;
      });
  test(
      "JSON MIDI URL -> HTTPS redirect -> binary download, no auth headers to "
      "CDN",
      [&] {
        Scenario s(folder.getChildFile("url"));
        HttpResponse redirect;
        redirect.status = 302;
        redirect.location = "https://cdn.example/final.mid";
        s.responses = {
            generated(), succeeded(),
            jsonResponse(
                R"({"code":200,"data":{"midi_url":"https://cdn.example/preview.mid"}})"),
            redirect, binaryResponse()};
        require(s.run().wasOk(), "URL branch failed");
        require(
            s.requests[3].headers.isEmpty() && s.requests[4].headers.isEmpty(),
            "CDN received API auth");
      });
  test("JSON base64 MIDI", [&] {
    Scenario s(folder.getChildFile("base64"));
    auto midi = fixtureMidi();
    auto data = juce::Base64::toBase64(midi.getData(), midi.getSize());
    s.responses = {
        generated(), succeeded(),
        jsonResponse("{\"code\":200,\"data\":{\"base64\":\"" + data + "\"}}")};
    require(s.run().wasOk(), "Base64 branch failed");
  });
  test("authentication failure is readable and never retried", [&] {
    Scenario s(folder.getChildFile("403"));
    s.responses = {
        jsonResponse(R"({"code":403,"msg":"invalid key test-key"})", 403)};
    auto result = s.run();
    require(result.failed() && result.getErrorMessage().contains("403"),
            "403 missing");
    require(!result.getErrorMessage().contains("test-key"),
            "Key leaked into UI");
    require(s.requests.size() == 1, "403 was retried");
  });
  test("ambiguous generate timeout is never retried", [&] {
    Scenario s(folder.getChildFile("timeout"));
    HttpResponse response;
    response.error = "connection timeout";
    s.responses = {response};
    require(s.run().failed() && s.requests.size() == 1,
            "Timeout generated another paid request");
  });
  test("definitively invalid model falls back once", [&] {
    Scenario s(folder.getChildFile("model"));
    s.responses = {jsonResponse(R"({"code":400,"msg":"invalid model"})", 400),
                   generated(), succeeded(), binaryResponse()};
    require(s.run().wasOk(), "Model fallback failed");
    require(
        juce::JSON::parse(s.requests[1].url.getPostData())["mv"].toString() ==
            "chirp-v4-5",
        "Wrong fallback model");
  });
  test("ordinary parameter errors do not trigger model retry", [&] {
    Scenario s(folder.getChildFile("400"));
    s.responses = {jsonResponse(R"({"code":400,"msg":"invalid prompt"})", 400)};
    require(s.run().failed() && s.requests.size() == 1,
            "Parameter error was retried");
  });
  test("feed may fall back to second documented host", [&] {
    Scenario s(folder.getChildFile("feed-host"));
    s.responses = {generated(), jsonResponse("{}", 404), succeeded(),
                   binaryResponse()};
    require(s.run().wasOk(), "Feed fallback failed");
    require(s.requests[2].url.getDomain() == "duomiapi.com",
            "Wrong fallback host");
  });
  test("empty generated task list fails without extraction", [&] {
    Scenario s(folder.getChildFile("empty"));
    s.responses = {jsonResponse(R"({"code":200,"data":[]})")};
    require(s.run().failed() && s.requests.size() == 1,
            "Empty result accepted");
  });
  test("upstream task failure is surfaced", [&] {
    Scenario s(folder.getChildFile("failed"));
    s.responses = {
        generated(),
        jsonResponse(
            R"({"code":200,"data":{"state":"error","msg":"upstream failed"}})")};
    auto result = s.run();
    require(
        result.failed() && result.getErrorMessage().contains("upstream failed"),
        "Task failure lost");
  });
  test("polling timeout preserves task for resume", [&] {
    Scenario s(folder.getChildFile("poll-timeout"));
    s.responses = {generated(), pending(), pending(), pending()};
    require(s.run().failed() && s.output.taskId == "first-task",
            "Polling never stopped or task lost");
  });
  test("resume skips billable generate", [&] {
    Scenario s(folder.getChildFile("resume"));
    s.responses = {succeeded(), binaryResponse()};
    require(s.run("existing-task").wasOk(), "Resume failed");
    require(s.requests[0].method == "GET" && s.output.taskId == "existing-task",
            "Resume regenerated music");
  });
  test(
      "live MIDI protocol: JSON body -> extraction ID -> feed notes -> "
      "standard MIDI",
      [&] {
        Scenario s(folder.getChildFile("async-midi"));
        s.responses = {generated(), succeeded(), midiSubmitted(), pending(),
                       midiCompleted()};
        auto result = s.run();
        require(result.wasOk(), result.getErrorMessage().toRawUTF8());
        require(s.output.midiTaskId == midiJobId && s.output.noteCount == 2,
                "Async MIDI task or notes lost");
        require(std::abs(s.output.durationSeconds - 2.0) < 0.001,
                "JSON seconds were treated as ticks");
        require(s.requests[3].url.toString(true).contains(midiJobId),
                "Queried music instead of MIDI task");
        juce::FileInputStream input(s.output.file);
        juce::MidiFile midi;
        require(midi.readFrom(input) && midi.getNumTracks() == 2,
                "Expected tempo + melodic track, no drums");
        midi.convertTimestampTicksToSeconds();
        const auto firstNote = [](const juce::MidiMessageSequence& track)
            -> const juce::MidiMessage* {
          for (const auto* event : track)
            if (event->message.isNoteOn()) return &event->message;
          return nullptr;
        };
        auto* first = firstNote(*midi.getTrack(1));
        require(first != nullptr && first->getChannel() == 1 &&
                    first->getVelocity() == 64 &&
                    std::abs(first->getTimeStamp() - 0.125) < 0.001,
                "Note timing, channel or normalized velocity changed");
        for (int t = 0; t < midi.getNumTracks(); ++t)
          for (const auto* event : *midi.getTrack(t))
            require(event->message.getChannel() != 10,
                    "Drums remained in saved MIDI");
      });
  test("MIDI resume only polls extraction; never resubmits generate or midi",
       [&] {
         Scenario s(folder.getChildFile("resume-midi"));
         s.responses = {midiCompleted()};
         require(s.run("existing-song", {}, midiJobId).wasOk(),
                 "MIDI resume failed");
         require(s.requests.size() == 1 &&
                     s.requests[0].url.toString(true).contains(
                         "/feed?task_id=" + midiJobId),
                 "MIDI resume submitted another billable request");
       });
  test("MIDI timeout retains extraction ID for safe retry", [&] {
    Scenario s(folder.getChildFile("midi-timeout"));
    s.responses = {generated(), succeeded(), midiSubmitted(),
                   pending(),   pending(),   pending()};
    require(s.run().failed() && s.output.midiTaskId == midiJobId,
            "Extraction ID lost on timeout");
  });
  test(
      "invalid note data is rejected instead of silently writing a corrupt "
      "MIDI",
      [&] {
        juce::MemoryBlock bytes;
        auto data = juce::JSON::parse(R"({"instruments":[{"program":0,"notes":[
      {"pitch":200,"start":0,"end":1,"velocity":0.5}]}]})");
        require(DuomiClient::notesToMidi(data, bytes).failed(),
                "Invalid pitch accepted");
        data = juce::JSON::parse(R"({"instruments":[{"program":0,"notes":[
      {"pitch":60,"start":0,"end":0,"velocity":0.5}]}]})");
        require(DuomiClient::notesToMidi(data, bytes).failed(),
                "Only zero-length notes accepted");
      });
  test("cancellation between submit and polling prevents further requests",
       [&] {
         Scenario s(folder.getChildFile("cancel"));
         s.responses = {generated()};
         auto result =
             s.run({}, [&](const juce::String&, const juce::String& task) {
               if (task.isNotEmpty()) s.cancellation.cancel();
             });
         require(result.failed() && s.requests.size() == 1,
                 "Cancellation ignored");
       });
  test("malformed download and empty MIDI are rejected", [&] {
    GeneratedMidi output;
    const juce::MemoryBlock html("<html>error</html>", 18);
    require(DuomiClient::parseMidi(html, output).failed(),
            "HTML accepted as MIDI");
    require(DuomiClient::parseMidi(fixtureMidi(false), output).failed(),
            "Empty MIDI accepted");
    auto truncated = fixtureMidi();
    truncated.setSize(14);
    require(DuomiClient::parseMidi(truncated, output).failed(),
            "Truncated MIDI accepted");
  });
  test("redaction handles URL-encoded keys", [&] {
    require(!DuomiClient::redact("?key=key%2Bsecret key+secret", "key+secret")
                 .contains("secret"),
            "Encoded key leaked");
  });
  test("worker publishes complete MIDI result and shuts down cleanly", [&] {
    ClientOptions options;
    options.outputDirectory = folder.getChildFile("worker");
    options.pollIntervalMs = 0;
    std::atomic<int> requests{0};
    GenerationJob job(options, [&](const HttpRequest&, Cancellation&) {
      switch (requests.fetch_add(1)) {
        case 0:
          return generated();
        case 1:
          return succeeded();
        default:
          return binaryResponse();
      }
    });
    require(job.start("test-key", "piano"), "Worker did not start");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    while (job.getSnapshot().busy &&
           juce::Time::getMillisecondCounterHiRes() < deadline)
      juce::Thread::sleep(5);
    auto state = job.getSnapshot();
    require(!state.busy && !state.failed && state.midi &&
                state.midi->noteCount == 1,
            "Worker did not publish result");
  });
  test("worker destruction cancels pending transport before joining", [&] {
    juce::WaitableEvent entered;
    std::atomic<bool> cancelled{false};
    ClientOptions options;
    options.outputDirectory = folder.getChildFile("shutdown");
    auto job = std::make_unique<GenerationJob>(
        options, [&](const HttpRequest&, Cancellation& token) {
          entered.signal();
          token.wait(3000);
          cancelled.store(token.isCancelled());
          HttpResponse response;
          response.error = "cancelled";
          return response;
        });
    require(job->start("test-key", "piano"), "Worker did not start");
    require(entered.wait(1000), "Worker never entered transport");
    require(!job->start("test-key", "another prompt"),
            "Concurrent generation accepted");
    auto before = juce::Time::getMillisecondCounterHiRes();
    job.reset();
    require(cancelled.load() &&
                juce::Time::getMillisecondCounterHiRes() - before < 1000,
            "Destruction failed to cancel pending I/O");
  });
  test(
      "worker resumes MIDI after timeout and preserves audio URL without "
      "resubmitting",
      [&] {
        ClientOptions options;
        options.outputDirectory = folder.getChildFile("worker-resume");
        options.pollIntervalMs = 0;
        options.maxPolls = 1;
        std::atomic<int> submits{0};
        std::atomic<int> requests{0};
        GenerationJob job(options,
                          [&](const HttpRequest& request, Cancellation&) {
                            if (request.url.getSubPath().endsWith("/midi"))
                              ++submits;
                            switch (requests.fetch_add(1)) {
                              case 0:
                                return generated();
                              case 1:
                                return succeeded();
                              case 2:
                                return midiSubmitted();
                              case 3:
                                return pending();
                              default:
                                return midiCompleted();
                            }
                          });
        const auto wait = [&] {
          const auto deadline =
              juce::Time::getMillisecondCounterHiRes() + 3000.0;
          while (job.getSnapshot().busy &&
                 juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::sleep(5);
          require(!job.getSnapshot().busy, "Worker did not finish");
        };
        require(job.start("test-key", "piano"), "Could not start extraction");
        wait();
        require(job.getSnapshot().failed &&
                    job.getSnapshot().midiTaskId == midiJobId,
                "Worker did not retain extraction ID after timeout");
        require(job.start("test-key", {}, true), "Could not resume extraction");
        wait();
        const auto state = job.getSnapshot();
        require(!state.failed && state.midi && submits.load() == 1 &&
                    requests.load() == 5,
                "Resume repeated a submission or did not finish");
        require(state.midi->audioUrl == "https://cdn.example/song.mp3",
                "Resume lost original audio preview URL");
      });
  test("editor initial state and validation error render without network", [&] {
    DuomiSunoAIBridgeAudioProcessor processor;
    auto initialStatus = processor.getGenerationJob().getSnapshot().status;
    require(initialStatus.length() == 2 && initialStatus[0] == 0x5c31 &&
                initialStatus[1] == 0x7eea,
            "Chinese status text was not decoded as UTF-8");
    std::unique_ptr<juce::AudioProcessorEditor> editor(
        processor.createEditor());
    editor->setVisible(true);
    auto writeSnapshot = [&](const char* filename) {
      auto image = editor->createComponentSnapshot(editor->getLocalBounds());
      auto stream = folder.getChildFile(filename).createOutputStream();
      require(
          stream && juce::PNGImageFormat().writeImageToStream(image, *stream),
          "UI render failed");
    };
    writeSnapshot("editor-ready.png");
    require(!processor.getGenerationJob().start({}, {}),
            "Empty credentials accepted");
    processor.getGenerationJob().sendSynchronousChangeMessage();
    require(processor.getGenerationJob().getSnapshot().failed,
            "Validation error not exposed");
    writeSnapshot("editor-validation.png");
    std::cout << "UI snapshots: " << folder.getFullPathName() << std::endl;
  });
  test("BPM wheel, double-click edit, commit/cancel and limits", [&] {
    cyber::BpmControl bpm;
    bpm.setBounds(0, 0, 108, 34);
    juce::MouseEvent event{
        juce::Desktop::getInstance().getMainMouseSource(),
        {10, 10},
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1,
        0,
        0,
        0,
        0,
        &bpm,
        &bpm,
        juce::Time::getCurrentTime(),
        {10, 10},
        juce::Time::getCurrentTime(),
        2,
        false};
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.1f;
    bpm.mouseWheelMove(event, wheel);
    require(bpm.getBpm() == 121, "Wheel up did not increase BPM");
    wheel.deltaY = -0.1f;
    bpm.mouseWheelMove(event, wheel);
    require(bpm.getBpm() == 120, "Wheel down did not decrease BPM");
    static_cast<juce::Component&>(bpm).mouseDoubleClick(event);
    require(bpm.isBeingEdited(), "Double-click did not open BPM editor");
    bpm.getCurrentTextEditor()->setText("147");
    bpm.hideEditor(false);
    require(bpm.getBpm() == 147, "Typed BPM was not committed");
    bpm.showEditor();
    bpm.getCurrentTextEditor()->setText("200");
    bpm.hideEditor(true);
    require(bpm.getBpm() == 147, "Cancelled BPM was applied");
    bpm.setText("", juce::sendNotificationSync);
    require(bpm.getBpm() == 147 && bpm.getText() == "147",
            "Empty BPM corrupted value");
    bpm.setText("999", juce::sendNotificationSync);
    require(bpm.getBpm() == 300, "BPM upper bound missing");
    bpm.setBpm(0);
    require(bpm.getBpm() == 20, "BPM lower bound missing");
    bpm.setEnabled(false);
    wheel.deltaY = 0.1f;
    bpm.mouseWheelMove(event, wheel);
    require(bpm.getBpm() == 20, "Disabled BPM changed");
  });
  test("description and BPM share the actual generate prompt", [&] {
    Scenario s(folder.getChildFile("bpm-prompt"));
    s.responses = {generated(), succeeded(), binaryResponse()};
    DuomiClient client(s.options, [&](const HttpRequest& r, Cancellation&) {
      require(s.requests.size() < s.responses.size(), "Unexpected BPM request");
      s.requests.push_back(r);
      return s.responses[s.requests.size() - 1];
    });
    const auto prompt = generationPrompt("  piano melody in C major  ", 147);
    require(
        client.generate("test-key", prompt, s.cancellation, s.output).wasOk(),
        "Generation with BPM failed");
    require(juce::JSON::parse(s.requests[0].url.getPostData())["prompt"]
                    .toString() == "piano melody in C major, 147 BPM",
            "BPM not sent as a keyword");
    require(generationPrompt("  ", 147).isEmpty(),
            "BPM bypassed empty description validation");
  });
  test("BPM survives closing and reopening the plugin editor", [&] {
    DuomiSunoAIBridgeAudioProcessor processor;
    auto editor =
        std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    const auto findBpm = [](juce::Component& c) -> cyber::BpmControl* {
      for (auto* child : c.getChildren())
        if (auto* bpm = dynamic_cast<cyber::BpmControl*>(child)) return bpm;
      return nullptr;
    };
    auto* bpm = findBpm(*editor);
    require(bpm != nullptr, "BPM field missing");
    bpm->setBpm(156);
    editor.reset();
    editor.reset(processor.createEditor());
    require(findBpm(*editor)->getBpm() == 156, "Reopening reset BPM");
  });

  const auto mixedMidi = [](bool smpte, int type) {
    juce::MidiFile midi;
    if (smpte)
      midi.setSmpteTimeFormat(25, 40);
    else
      midi.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence mixed;
    mixed.addEvent(juce::MidiMessage::tempoMetaEvent(500000));
    auto tempo = juce::MidiMessage::tempoMetaEvent(1000000);
    tempo.setTimeStamp(480);
    mixed.addEvent(tempo);
    mixed.addEvent(juce::MidiMessage::timeSignatureMetaEvent(3, 4));
    mixed.addEvent(juce::MidiMessage::programChange(1, 0));
    mixed.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127));
    mixed.addEvent(juce::MidiMessage::pitchWheel(1, 9000));
    mixed.addEvent(juce::MidiMessage::programChange(10, 0));
    mixed.addEvent(juce::MidiMessage::controllerEvent(10, 7, 100));
    for (int pitch : {60, 64, 67, 36}) {
      const int channel = pitch == 36 ? 10 : 1;
      mixed.addEvent(
          juce::MidiMessage::noteOn(channel, pitch, (juce::uint8)91));
      auto off = juce::MidiMessage::noteOff(channel, pitch);
      off.setTimeStamp(960);
      mixed.addEvent(off);
    }
    midi.addTrack(mixed);
    juce::MemoryOutputStream stream;
    require(midi.writeTo(stream, type), "Could not create percussion fixture");
    return stream.getMemoryBlock();
  };
  test(
      "binary generation removes channel 10 from mixed tracks, preserves "
      "chords and tempo",
      [&] {
        Scenario s(folder.getChildFile("mixed-drums"));
        auto response = binaryResponse();
        response.body = mixedMidi(false, 0);
        s.responses = {generated(), succeeded(), response};
        require(s.run().wasOk(), "Mixed-track generation failed");
        require(s.output.noteCount == 3 &&
                    std::abs(s.output.durationSeconds - 1.5) < 1e-6,
                "Chords or tempo map changed during filtering");
        juce::FileInputStream input(s.output.file);
        juce::MidiFile midi;
        int type = -1;
        require(midi.readFrom(input, false, &type) && type == 0 &&
                    midi.getTimeFormat() == 480,
                "File format or PPQ changed");
        bool meter = false, controller = false, bend = false;
        for (int t = 0; t < midi.getNumTracks(); ++t)
          for (const auto* e : *midi.getTrack(t)) {
            const auto& m = e->message;
            require(m.getChannel() != 10,
                    "Percussion event reached exported file");
            if (m.isNoteOn())
              require(m.getVelocity() == 91, "Velocity changed");
            meter |= m.isTimeSignatureMetaEvent();
            controller |= m.isController();
            bend |= m.isPitchWheel();
          }
        require(meter && controller && bend,
                "Melodic expression or meter lost");
      });
  test("SMPTE timing preserved when removing percussion", [&] {
    auto bytes = mixedMidi(true, 1);
    require(DuomiClient::filterPercussion(bytes).wasOk(),
            "SMPTE filtering failed");
    GeneratedMidi result;
    require(DuomiClient::parseMidi(bytes, result).wasOk() &&
                result.noteCount == 3 &&
                std::abs(result.durationSeconds - 0.96) < 1e-6,
            "SMPTE timing changed");
  });
  test(
      "named percussion removed on nonstandard channels; drum-track tempo "
      "retained",
      [&] {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(480);
        juce::MidiMessageSequence drums, melody;
        drums.addEvent(juce::MidiMessage::textMetaEvent(4, "Percussion"));
        drums.addEvent(juce::MidiMessage::tempoMetaEvent(1000000));
        for (auto* seq : {&drums, &melody}) {
          const int channel = seq == &drums ? 2 : 1;
          seq->addEvent(juce::MidiMessage::noteOn(channel, 60, 0.8f));
          auto off = juce::MidiMessage::noteOff(channel, 60);
          off.setTimeStamp(480);
          seq->addEvent(off);
        }
        melody.addEvent(juce::MidiMessage::textMetaEvent(3, "Steel Drums"));
        midi.addTrack(drums);
        midi.addTrack(melody);
        juce::MemoryOutputStream stream;
        require(midi.writeTo(stream), "Named-track fixture failed");
        auto bytes = stream.getMemoryBlock();
        require(DuomiClient::filterPercussion(bytes).wasOk(),
                "Named drum filtering failed");
        GeneratedMidi result;
        require(DuomiClient::parseMidi(bytes, result).wasOk() &&
                    result.noteCount == 1 &&
                    std::abs(result.durationSeconds - 1.0) < 1e-6,
                "Pitched instrument or tempo on removed track lost");
        for (const auto* e : result.sequence)
          require(e->message.getChannel() == 1,
                  "Nonstandard percussion channel retained");
      });
  test("drums-only binary generation fails without publishing a MIDI file",
       [&] {
         juce::MidiFile midi;
         midi.setTicksPerQuarterNote(480);
         juce::MidiMessageSequence drums;
         drums.addEvent(juce::MidiMessage::noteOn(10, 36, 0.9f));
         auto off = juce::MidiMessage::noteOff(10, 36);
         off.setTimeStamp(240);
         drums.addEvent(off);
         midi.addTrack(drums);
         juce::MemoryOutputStream stream;
         require(midi.writeTo(stream), "Drum-only fixture failed");
         auto response = binaryResponse();
         response.body = stream.getMemoryBlock();
         Scenario s(folder.getChildFile("only-drums"));
         s.responses = {generated(), succeeded(), response};
         require(s.run().failed() && s.output.file == juce::File{},
                 "Drum-only result published");
       });
  test("JSON ignores marked or named percussion before note validation", [&] {
    const auto json = juce::JSON::parse(R"({"instruments":[
      {"name":"Piano","program":0,"is_drum":false,
       "notes":[{"pitch":60,"start":0,"end":1,"velocity":0.8}]},
      {"name":"Kit","is_drum":true},
      {"name":"Percussion","program":0} ]})");
    juce::MemoryBlock bytes;
    GeneratedMidi result;
    require(DuomiClient::notesToMidi(json, bytes).wasOk() &&
                DuomiClient::parseMidi(bytes, result).wasOk() &&
                result.noteCount == 1,
            "Percussion was validated or emitted as melody");
    const auto onlyDrums =
        juce::JSON::parse(R"({"instruments":[{"is_drum":true}]})");
    require(DuomiClient::notesToMidi(onlyDrums, bytes).failed(),
            "Drum-only JSON accepted");
  });
  test(
      "a drum-named mixed track retains melodic channels in type 0 and 1", [&] {
        for (const int type : {0, 1}) {
          auto bytes = mixedMidi(false, type);
          juce::MemoryInputStream input(bytes, false);
          juce::MidiFile original, titled;
          require(original.readFrom(input, false), "Cannot read mixed fixture");
          titled.setTicksPerQuarterNote(original.getTimeFormat());
          auto track = *original.getTrack(0);
          track.addEvent(
              juce::MidiMessage::textMetaEvent(3, "Drums and Piano"));
          titled.addTrack(track);
          juce::MemoryOutputStream stream;
          require(titled.writeTo(stream, type), "Cannot write titled fixture");
          bytes = stream.getMemoryBlock();
          GeneratedMidi output;
          require(DuomiClient::filterPercussion(bytes).wasOk() &&
                      DuomiClient::parseMidi(bytes, output).wasOk() &&
                      output.noteCount == 3 &&
                      std::abs(output.durationSeconds - 1.5) < 1e-6,
                  "Track title caused melodic chords to be deleted");
          for (const auto* event : output.sequence)
            require(event->message.getChannel() != 10,
                    "Mixed track still has drums");
        }
      });
  test("Hi-Hat variants are removed; English and Chinese steel drums preserved",
       [&] {
         for (const auto& name :
              juce::StringArray{"Hi-Hat", "Hi Hat", "HIHAT", "Steel Drums",
                                juce::String::fromUTF8("钢鼓")}) {
           const int expected = name.startsWithIgnoreCase("hi") ? 1 : 2;
           auto data = juce::JSON::parse(R"({"instruments":[
        {"name":"Piano","program":0,"notes":[
          {"pitch":60,"start":0,"end":1,"velocity":0.8}]},
        {"name":"","program":114,"notes":[
          {"pitch":64,"start":0,"end":1,"velocity":0.8}]}]})");
           data["instruments"]
               .getArray()
               ->getReference(1)
               .getDynamicObject()
               ->setProperty("name", name);
           juce::MemoryBlock jsonBytes;
           GeneratedMidi jsonOutput;
           require(DuomiClient::notesToMidi(data, jsonBytes).wasOk() &&
                       DuomiClient::parseMidi(jsonBytes, jsonOutput).wasOk() &&
                       jsonOutput.noteCount == expected,
                   "JSON name classification failed");

           juce::MidiFile midi;
           midi.setTicksPerQuarterNote(480);
           for (int channel : {1, 2}) {
             juce::MidiMessageSequence track;
             track.addEvent(juce::MidiMessage::textMetaEvent(
                 3, channel == 1 ? juce::String("Piano") : name));
             track.addEvent(juce::MidiMessage::noteOn(channel, 60, 0.8f));
             auto off = juce::MidiMessage::noteOff(channel, 60);
             off.setTimeStamp(480);
             track.addEvent(off);
             midi.addTrack(track);
           }
           juce::MemoryOutputStream stream;
           require(midi.writeTo(stream), "Cannot write named fixture");
           auto bytes = stream.getMemoryBlock();
           GeneratedMidi output;
           require(DuomiClient::filterPercussion(bytes).wasOk() &&
                       DuomiClient::parseMidi(bytes, output).wasOk() &&
                       output.noteCount == expected,
                   "Binary name classification failed");
         }
       });
  std::cout << passed
            << " offline scenarios passed. No live API calls were made."
            << std::endl;
  return 0;
}

int liveProbe(const juce::String& taskId, const juce::String& midiTaskId = {}) {
  std::cerr
      << "Live Duomi test: reads API key from stdin. --live submits music; "
         "--resume skips music; --resume-midi only reads an existing "
         "extraction.\n";
  std::string key;
  std::getline(std::cin, key);
  ClientOptions options;
  options.logFile = options.outputDirectory.getChildFile(
      "probe_" + juce::Uuid().toString() + ".log");
  Cancellation cancel;
  GeneratedMidi output;
  DuomiClient client(options);
  const auto result = client.generate(
      juce::String(key).trim(),
      "Short solo piano instrumental, simple C major melody, 120 BPM, no "
      "vocals.",
      cancel, output,
      [&output](const juce::String& status, const juce::String& task) {
        std::cout << status << " task_id=" << task
                  << " midi_task_id=" << output.midiTaskId << std::endl;
      },
      taskId, midiTaskId);
  std::cout << "Log: " << options.logFile.getFullPathName() << std::endl;
  if (result.failed()) {
    std::cerr << result.getErrorMessage() << std::endl;
    return 1;
  }
  std::cout << "MIDI: " << output.file.getFullPathName()
            << " notes=" << output.noteCount
            << " seconds=" << output.durationSeconds << std::endl;
  return 0;
}

int nativeHttpCheck(int port) {
  require(port > 0 && port <= 65535, "Invalid loopback port");
  const auto host = "http://127.0.0.1:" + juce::String(port);
  ClientOptions options;
  options.generateHosts = {host};
  options.midiHosts = {host};
  options.pollIntervalMs = 0;
  options.maxPolls = 3;
  options.outputDirectory =
      juce::File::getCurrentWorkingDirectory().getChildFile(
          "build/live-validation/native-loopback");
  Cancellation cancel;
  GeneratedMidi output;
  DuomiClient client(
      options, [](const HttpRequest& request, Cancellation& token) {
        auto response = DuomiClient::requestNative(request, token);
        if (response.error.isNotEmpty()) {
          std::cerr << "Loopback URL: " << request.url.toString(false)
                    << " HTTP " << response.status;
          std::cerr << std::endl;
        }
        return response;
      });
  auto result = client.generate("loopback-test-key", "piano", cancel, output);
  require(result.wasOk(), result.getErrorMessage().toRawUTF8());
  require(
      output.noteCount == 1 && std::abs(output.durationSeconds - 1.5) < 0.001,
      "Native HTTP pipeline did not produce expected MIDI");
  std::cout << "PASS native HTTP JSON GET body and async MIDI conversion\n";
  return 0;
}
}  // namespace

int runTrackSelectionTests();
int stemShowcase();
int stemLiveProbe(const juce::String&);
int runChordTests();
int runSessionTests();
int chordProbe(const juce::File&, const juce::File&);
int runApiKeyStoreTests();
int runAudioPreviewTests();
int audioPreviewProbe(const juce::String&, const juce::File&);
int runMidiTransferTests();
int exportMidiCheck(const juce::File&, const juce::File&);
int runMidiPreviewTests();
int renderMidiPreview(const juce::File&, const juce::File&);
int main(int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI initialise;
  try {
    if (argc == 1) return offlineTests();
    if (argc == 2 && juce::String(argv[1]) == "--stem-showcase")
      return stemShowcase();
    if (argc == 2 && juce::String(argv[1]) == "--track-tests")
      return runTrackSelectionTests();
    if (argc == 3 && juce::String(argv[1]) == "--separate-stems")
      return stemLiveProbe(argv[2]);
    if (argc == 2 && juce::String(argv[1]) == "--chord-tests")
      return runChordTests();
    if (argc == 2 && juce::String(argv[1]) == "--session-tests")
      return runSessionTests();
    if (argc == 4 && juce::String(argv[1]) == "--extract-chords")
      return chordProbe(
          juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]),
          juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]));
    if (argc == 2 && juce::String(argv[1]) == "--key-tests")
      return runApiKeyStoreTests();
    if (argc == 2 && juce::String(argv[1]) == "--audio-tests")
      return runAudioPreviewTests();
    if (argc == 4 && juce::String(argv[1]) == "--render-audio")
      return audioPreviewProbe(
          juce::String::fromUTF8(argv[2]),
          juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]));
    if (argc == 2 && juce::String(argv[1]) == "--transfer-tests")
      return runMidiTransferTests();
    if (argc == 4 && juce::String(argv[1]) == "--export-midi")
      return exportMidiCheck(
          juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]),
          juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]));
    if (argc == 2 && juce::String(argv[1]) == "--preview-tests")
      return runMidiPreviewTests();
    if (argc == 4 && juce::String(argv[1]) == "--render-preview")
      return renderMidiPreview(
          juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]),
          juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]));
    if (argc == 2 && juce::String(argv[1]) == "--live") return liveProbe({});
    if (argc == 3 && juce::String(argv[1]) == "--resume")
      return liveProbe(argv[2]);
    if (argc == 3 && juce::String(argv[1]) == "--resume-midi")
      return liveProbe({}, argv[2]);
    if (argc == 3 && juce::String(argv[1]) == "--native-http-check")
      return nativeHttpCheck(juce::String(argv[2]).getIntValue());
    std::cerr << "Usage: DuomiBridgeCheck [--live | --resume TASK_ID | "
                 "--resume-midi MIDI_TASK_ID]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << std::endl;
    return 1;
  }
}
