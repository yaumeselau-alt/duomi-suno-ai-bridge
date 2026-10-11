#include <cmath>
#include <iostream>
#include <stdexcept>

#include "PluginProcessor.h"
namespace {
struct ExportTestDirectory {
  juce::File path =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("duomi-audio-exports-" + juce::Uuid().toString());
  ~ExportTestDirectory() { path.deleteRecursively(); }
};
ExportTestDirectory testExports;
class AudioPreviewPlayer : public duomi::AudioPreviewPlayer {
 public:
  explicit AudioPreviewPlayer(duomi::DuomiClient::Transport transport = {})
      : duomi::AudioPreviewPlayer(std::move(transport), testExports.path) {}
};
using State = AudioPreviewPlayer::State;
void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void wait(duomi::AudioPreviewPlayer& p) {
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000;
  while (p.getSnapshot().busy &&
         juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::Thread::sleep(2);
  check(!p.getSnapshot().busy, "Audio worker timed out");
  // Busy is published just before Thread::run returns.
  juce::Thread::sleep(5);
}
juce::MemoryBlock wav(int channels = 2, int rate = 24000) {
  juce::MemoryBlock result;
  juce::WavAudioFormat format;
  auto* stream = new juce::MemoryOutputStream(result, false);
  std::unique_ptr<juce::AudioFormatWriter> writer(
      format.createWriterFor(stream, rate, channels, 16, {}, 0));
  check(writer != nullptr, "Cannot create fixture");
  juce::AudioBuffer<float> audio(channels, rate / 2);
  for (int ch = 0; ch < channels; ++ch)
    for (int i = 0; i < audio.getNumSamples(); ++i)
      audio.setSample(
          ch, i,
          static_cast<float>(
              0.25 *
              std::sin(i * 440.0 * juce::MathConstants<double>::twoPi / rate)) *
              (ch == 1 ? -1.0f : 1.0f));
  check(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
        "Cannot write fixture");
  writer.reset();
  return result;
}
duomi::HttpResponse response(juce::MemoryBlock bytes) {
  duomi::HttpResponse r;
  r.status = 200;
  r.body = std::move(bytes);
  return r;
}
void valid(const juce::AudioBuffer<float>& b) {
  for (int ch = 0; ch < b.getNumChannels(); ++ch)
    for (int i = 0; i < b.getNumSamples(); ++i)
      check(std::isfinite(b.getSample(ch, i)) &&
                std::abs(b.getSample(ch, i)) <= 1.0f,
            "Invalid audio sample");
}
std::vector<float> rendered(int block, double rate, int channels = 2) {
  auto bytes = wav(channels);
  AudioPreviewPlayer p([&](const auto&, auto&) { return response(bytes); });
  p.prepare(rate);
  check(p.download("https://audio.invalid/song.wav"), "Start failed");
  wait(p);
  check(p.getSnapshot().ready && !p.getSnapshot().failed, "Decode failed");
  p.play();
  std::vector<float> result;
  for (int remaining = static_cast<int>(rate * 0.6); remaining > 0;) {
    int count = juce::jmin(remaining, block);
    juce::AudioBuffer<float> b(2, count);
    p.render(b);
    valid(b);
    for (int i = 0; i < count; ++i)
      check(std::abs(b.getSample(0, i) -
                     (channels == 1 ? b.getSample(1, i) : -b.getSample(1, i))) <
                1e-4f,
            "Channel mapping failed");
    result.insert(result.end(), b.getReadPointer(0),
                  b.getReadPointer(0) + count);
    remaining -= count;
  }
  check(p.getSnapshot().state == State::finished, "Audio did not finish");
  check(std::abs(p.getSnapshot().durationSeconds - 0.5) < 1e-5,
        "Duration wrong");
  p.play();
  juce::AudioBuffer<float> b(2, 512);
  p.render(b);
  check(p.getSnapshot().state == State::playing &&
            p.getSnapshot().positionSeconds < 0.1,
        "Replay failed");
  return result;
}
}  // namespace
int runAudioPreviewTests() {
  int passed = 0;
  auto test = [&](const char* name, auto fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << std::endl;
  };
  test("PCM resampling, stereo/mono, block independence, finish and replay",
       [] {
         for (double rate : {44100.0, 48000.0, 96000.0})
           for (int channels : {1, 2}) {
             auto a = rendered(64, rate, channels),
                  b = rendered(511, rate, channels);
             float peak = 0;
             check(a.size() == b.size(), "Size mismatch");
             for (size_t i = 0; i < a.size(); ++i) {
               check(std::abs(a[i] - b[i]) < 1e-6f, "Block size changes audio");
               peak = juce::jmax(peak, std::abs(a[i]));
             }
             check(peak > 0.2f, "Silent playback");
           }
       });
  test("pause freeze, resume, stop/play commands and release", [] {
    AudioPreviewPlayer p([](const auto&, auto&) { return response(wav()); });
    p.prepare(48000);
    p.download("https://audio.invalid/a");
    wait(p);
    p.play();
    juce::AudioBuffer<float> b(2, 1024);
    p.render(b);
    auto before = p.getSnapshot().positionSeconds;
    p.pause();
    p.render(b);
    p.render(b);
    check(p.getSnapshot().state == State::paused &&
              p.getSnapshot().positionSeconds == before,
          "Pause advanced");
    check(b.getMagnitude(0, b.getNumSamples()) == 0, "Pause not silent");
    p.play();
    p.render(b);
    check(p.getSnapshot().positionSeconds > before, "Resume failed");
    p.stop();
    p.play();
    p.render(b);
    check(std::abs(p.getSnapshot().positionSeconds - 1024.0 / 48000) < 1e-6,
          "Stop/play did not reset");
    p.stop();
    p.render(b);
    p.render(b);
    check(p.getSnapshot().positionSeconds == 0 &&
              b.getMagnitude(0, b.getNumSamples()) == 0,
          "Stop failed");
    p.release();
    p.render(b);
    check(b.getMagnitude(0, b.getNumSamples()) == 0, "Release failed");
    p.prepare(44100);
    p.play();
    p.render(b);
    check(p.getSnapshot().state == State::playing, "Prepare lost clip");
  });
  test("HTTPS redirects, no credentials, same-source cache", [] {
    int calls = 0;
    AudioPreviewPlayer p([&](const duomi::HttpRequest& req, auto&) {
      ++calls;
      check(req.headers.isEmpty() && req.method == "GET",
            "Credentials forwarded");
      duomi::HttpResponse r;
      if (calls == 1) {
        r.status = 302;
        r.location = "next.wav";
      } else {
        check(req.url.toString(true) == "https://audio.invalid/dir/next.wav",
              "Relative redirect wrong");
        r = response(wav());
      }
      return r;
    });
    check(!p.download("http://audio.invalid/a"), "Insecure URL accepted");
    p.download("https://audio.invalid/dir/song.wav?token=example");
    wait(p);
    check(p.getSnapshot().ready, "Redirect download failed");
    check(p.download("https://audio.invalid/dir/song.wav?token=example") &&
              calls == 2,
          "Cache downloaded twice");
  });
  test("HTTP/decode errors preserve prior audio and allow retry", [] {
    int kind = 0;
    AudioPreviewPlayer p([&](const auto&, auto&) {
      if (kind == 0) return response(wav());
      duomi::HttpResponse r;
      if (kind == 1)
        r.status = 403;
      else if (kind == 2) {
        r.status = 200;
        r.body.append("bad", 3);
      } else {
        r.status = 302;
        r.location = "http://insecure.invalid/a";
      }
      return r;
    });
    p.prepare(48000);
    p.download("https://audio.invalid/good");
    wait(p);
    juce::AudioBuffer<float> b(2, 512);
    p.render(b);
    for (kind = 1; kind <= 3; ++kind) {
      p.download("https://audio.invalid/fail" + juce::String(kind));
      wait(p);
      check(p.getSnapshot().failed && p.getSnapshot().ready,
            "Failure lost old clip");
      p.stop();
      p.play();
      p.render(b);
      check(b.getMagnitude(0, b.getNumSamples()) > 0.1f,
            "Old clip not playable");
    }
    kind = 0;
    p.download("https://audio.invalid/retry");
    wait(p);
    check(!p.getSnapshot().failed, "Retry failed");
  });
  test("bounded redirects and decode limits", [] {
    int count = 0;
    AudioPreviewPlayer loop([&](const auto&, auto&) {
      ++count;
      duomi::HttpResponse r;
      r.status = 302;
      r.location = "/loop";
      return r;
    });
    loop.download("https://audio.invalid/loop");
    wait(loop);
    check(loop.getSnapshot().failed && count == 4, "Redirects not bounded");
    AudioPreviewPlayer surround(
        [](const auto&, auto&) { return response(wav(3)); });
    surround.download("https://audio.invalid/three");
    wait(surround);
    check(surround.getSnapshot().failed, "Unsupported channel count accepted");
  });
  test("cancel, busy rejection, worker destruction and retained preview", [] {
    std::atomic<bool> blocked{false};
    auto transport = [&](const auto&, duomi::Cancellation& token) {
      if (blocked) {
        while (!token.isCancelled()) token.wait(10);
      }
      return response(wav());
    };
    AudioPreviewPlayer p(transport);
    p.download("https://audio.invalid/a");
    wait(p);
    blocked = true;
    p.download("https://audio.invalid/b");
    check(!p.download("https://audio.invalid/c"),
          "Concurrent download accepted");
    p.cancel();
    wait(p);
    check(!p.getSnapshot().failed && p.getSnapshot().ready,
          "Cancel lost preview");
    {
      AudioPreviewPlayer closing(transport);
      closing.download("https://audio.invalid/d");
    }
  });
  test("local import, replacement without editor, mono downmix", [] {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("duomi-audio-test-" + juce::Uuid().toString());
    dir.createDirectory();
    auto file = dir.getChildFile(juce::String::fromUTF8("本地 音频.wav"));
    auto bytes = wav();
    check(file.replaceWithData(bytes.getData(), bytes.getSize()),
          "Fixture save failed");
    AudioPreviewPlayer p;
    p.prepare(48000);
    juce::AudioBuffer<float> b(1, 1024);
    for (int i = 0; i < 4; ++i) {
      p.importFile(file);
      wait(p);
      p.play();
      p.render(b);
      check(!p.getSnapshot().failed && p.getSnapshot().durationSeconds == 0.5,
            "Import/replacement failed");
      check(b.getMagnitude(0, b.getNumSamples()) < 1e-4,
            "Stereo mono downmix wrong");
    }
    p.collectRetired();
    p.importFile(dir.getChildFile("missing.wav"));
    wait(p);
    check(p.getSnapshot().failed && p.getSnapshot().ready,
          "Missing file lost preview");
    dir.deleteRecursively();
  });
  test("processor mode routing and editor lifetime", [] {
    auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("duomi-audio-" + juce::Uuid().toString() + ".wav");
    auto bytes = wav();
    file.replaceWithData(bytes.getData(), bytes.getSize());
    DuomiSunoAIBridgeAudioProcessor p;
    p.prepareToPlay(48000, 512);
    p.getAudioPreviewPlayer().importFile(file);
    wait(p.getAudioPreviewPlayer());
    p.selectAudioPreview(true);
    p.playPreview();
    juce::AudioBuffer<float> b(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(b, midi);
    check(b.getMagnitude(0, b.getNumSamples()) > 0.1, "Processor audio silent");
    {
      std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
      check(editor->getHeight() == 844, "Wrong P5 editor size");
    }
    auto pos = p.getAudioPreviewPlayer().getSnapshot().positionSeconds;
    p.processBlock(b, midi);
    check(p.getAudioPreviewPlayer().getSnapshot().positionSeconds > pos,
          "Closing editor stopped audio");
    p.selectAudioPreview(false);
    p.processBlock(b, midi);
    check(b.getMagnitude(0, b.getNumSamples()) == 0,
          "Audio leaks into MIDI mode");
    p.selectAudioPreview(true);
    p.processBlock(b, midi);
    p.processBlock(b, midi);
    check(b.getMagnitude(0, b.getNumSamples()) == 0, "Mode switch didn't stop");
    p.releaseResources();
    file.deleteFile();
  });
  test("waveform peaks and seeking while stopped, playing, paused and finished",
       [] {
         AudioPreviewPlayer player(
             [](const auto&, auto&) { return response(wav()); });
         player.prepare(48000);
         player.download("https://audio.invalid/wave");
         wait(player);
         const auto wave = player.getSnapshot().waveform;
         check(
             wave && wave->peaks.size() == 2048 && wave->durationSeconds == 0.5,
             "Missing waveform summary");
         float low = 0, high = 0;
         for (const auto& peak : wave->peaks) {
           low = juce::jmin(low, peak.low);
           high = juce::jmax(high, peak.high);
         }
         check(low < -0.24f && high > 0.24f,
               "Stereo peaks cancelled or clipped");
         juce::AudioBuffer<float> b(2, 480);
         player.render(b);
         player.seek(0.5);
         player.render(b);
         check(player.getSnapshot().state == State::paused &&
                   std::abs(player.getSnapshot().positionSeconds - 0.25) < 1e-6,
               "Stopped seek failed");
         player.play();
         player.render(b);
         check(std::abs(player.getSnapshot().positionSeconds - 0.26) < 1e-6 &&
                   b.getMagnitude(0, 480) > 0.1,
               "Seek/play did not continue");
         player.seek(0.2);
         player.render(b);
         check(player.getSnapshot().state == State::playing &&
                   std::abs(player.getSnapshot().positionSeconds - 0.11) < 1e-6,
               "Playing seek failed");
         player.pause();
         player.render(b);
         player.seek(0.8);
         player.render(b);
         player.render(b);
         check(
             player.getSnapshot().state == State::paused &&
                 std::abs(player.getSnapshot().positionSeconds - 0.4) < 1e-6 &&
                 b.getMagnitude(0, 480) == 0,
             "Paused seek did not freeze");
         player.seek(2.0);
         player.render(b);
         check(player.getSnapshot().state == State::finished &&
                   player.getSnapshot().positionSeconds == 0.5,
               "End clamp failed");
         player.seek(-2.0);
         player.render(b);
         check(player.getSnapshot().state == State::paused &&
                   player.getSnapshot().positionSeconds == 0,
               "Start clamp failed");
         player.seek(0.6);
         player.stop();
         player.render(b);
         check(player.getSnapshot().state == State::stopped &&
                   player.getSnapshot().positionSeconds == 0,
               "Stop did not discard seek");
         player.seek(0.1);
         player.seek(0.7);
         player.render(b);
         check(std::abs(player.getSnapshot().positionSeconds - 0.35) < 1e-6,
               "Latest drag position lost");
         player.importFile(
             juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("missing-wave-" + juce::Uuid().toString()));
         wait(player);
         check(player.getSnapshot().waveform == wave,
               "Failed load lost waveform");
       });
  test(
      "audio drag WAV is full length, un-faded, persistent and failure "
      "preserves preview",
      [] {
        juce::File exported;
        {
          AudioPreviewPlayer player(
              [](const auto&, auto&) { return response(wav()); });
          player.download("https://audio.invalid/export");
          wait(player);
          exported = player.getSnapshot().dragFile;
          check(exported.existsAsFile() && exported.hasFileExtension("wav"),
                "Missing drag WAV");
          juce::AudioFormatManager formats;
          formats.registerBasicFormats();
          std::unique_ptr<juce::AudioFormatReader> reader(
              formats.createReaderFor(exported));
          check(reader && reader->sampleRate == 24000 &&
                    reader->numChannels == 2 &&
                    reader->lengthInSamples == 12000 &&
                    reader->bitsPerSample == 24,
                "WAV format/length changed");
          juce::AudioBuffer<float> pcm(2, 32);
          check(reader->read(&pcm, 0, 32, 0, true, true),
                "Exported WAV unreadable");
          check(std::abs(pcm.getSample(0, 10)) > 0.2f,
                "Export contains playback fade");
          player.importFile(testExports.path.getChildFile("missing.wav"));
          wait(player);
          check(player.getSnapshot().dragFile == exported,
                "Failure lost previous export");
        }
        check(exported.existsAsFile(), "Export deleted on plugin close");
        auto blocked = testExports.path.getChildFile("blocked");
        blocked.replaceWithText("keep");
        duomi::AudioPreviewPlayer player(
            [](const auto&, auto&) { return response(wav()); },
            blocked.getChildFile("exports"));
        player.download("https://audio.invalid/blocked");
        wait(player);
        check(player.getSnapshot().ready && player.getSnapshot().failed &&
                  player.getSnapshot().dragFile == juce::File{},
              "WAV disk error broke preview or reported drag ready");
      });
  test(
      "thread-start failure clears busy, preserves previous audio and allows "
      "retry",
      [&] {
        class FailablePlayer : public AudioPreviewPlayer {
         public:
          using AudioPreviewPlayer::AudioPreviewPlayer;
          bool failStart = false;

         protected:
          bool startWorker() override {
            return !failStart && AudioPreviewPlayer::startWorker();
          }
        };
        int requests = 0;
        const auto bytes = wav();
        FailablePlayer p([&](const auto&, auto&) {
          ++requests;
          return response(bytes);
        });
        p.prepare(48000);
        check(p.download("https://audio.invalid/first.wav"),
              "First load failed");
        wait(p);
        const auto before = p.getSnapshot();
        check(before.ready, "First clip not ready");
        p.failStart = true;
        check(!p.download("https://audio.invalid/second.wav"),
              "Thread failure accepted");
        const auto after = p.getSnapshot();
        check(after.failed && !after.busy && after.ready && requests == 1 &&
                  after.waveform == before.waveform &&
                  after.dragFile == before.dragFile &&
                  after.source == before.source,
              "Failure lost old clip or left busy stuck");
        p.play();
        juce::AudioBuffer<float> buffer(2, 512);
        p.render(buffer);
        check(buffer.getMagnitude(0, 512) > 0, "Old audio no longer playable");
        p.failStart = false;
        check(p.download("https://audio.invalid/second.wav"),
              "Retry blocked after failure");
        wait(p);
        check(p.getSnapshot().ready && !p.getSnapshot().failed && requests == 2,
              "Retry did not recover");
      });
  std::cout << passed << " audio preview groups passed\n";
  return 0;
}
int audioPreviewProbe(const juce::String& source, const juce::File& output) {
  DuomiSunoAIBridgeAudioProcessor processor;
  processor.prepareToPlay(48000, 512);
  auto& player = processor.getAudioPreviewPlayer();
  if (source.startsWithIgnoreCase("https://"))
    player.download(source);
  else
    player.importFile(juce::File(source));
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 180000;
  while (player.getSnapshot().busy &&
         juce::Time::getMillisecondCounterHiRes() < deadline)
    juce::Thread::sleep(20);
  check(!player.getSnapshot().busy && player.getSnapshot().ready,
        player.getSnapshot().message.toRawUTF8());
  processor.selectAudioPreview(true);
  processor.playPreview();
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> b(2, 512);
  auto stream = output.createOutputStream();
  check(stream != nullptr, "Cannot open render output");
  stream->setPosition(0);
  stream->truncate();
  juce::WavAudioFormat format;
  std::unique_ptr<juce::AudioFormatWriter> writer(
      format.createWriterFor(stream.release(), 48000, 2, 16, {}, 0));
  check(writer != nullptr, "Cannot create render writer");
  float peak = 0;
  double squares = 0;
  juce::int64 frames = 0;
  do {
    processor.processBlock(b, midi);
    valid(b);
    peak = juce::jmax(peak, b.getMagnitude(0, b.getNumSamples()));
    for (int i = 0; i < b.getNumSamples(); ++i)
      squares += b.getSample(0, i) * b.getSample(0, i);
    frames += b.getNumSamples();
    writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
  } while (player.getSnapshot().state == State::playing &&
           frames < 48000 * 601LL);
  check(player.getSnapshot().state == State::finished && peak > 0,
        "Silent or unfinished audio");
  writer.reset();
  player.seek(0.35);
  processor.processBlock(b, midi);
  std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
  auto png = output.withFileExtension(".png").createOutputStream();
  if (png) {
    png->setPosition(0);
    png->truncate();
    juce::PNGImageFormat format;
    format.writeImageToStream(
        editor->createComponentSnapshot(editor->getLocalBounds()), *png);
  }
  std::cout << "PASS audio preview seconds="
            << player.getSnapshot().durationSeconds << " peak=" << peak
            << " rms=" << std::sqrt(squares / frames) << std::endl;
  return 0;
}