#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace duomi {

struct MidiPart {
  int track = 0, channel = 1;
  juce::String name;
};
struct AudioStem {
  juce::String taskId, name, url, file;
};
struct StemBatch {
  juce::String sourceTask, clipId;
  bool submissionUncertain = false;
  std::vector<AudioStem> stems;
};
struct GeneratedMidi {
  std::vector<MidiPart> parts;
  juce::File file;
  juce::MemoryBlock
      fileBytes;  // Original tick-based MIDI, safe for host-state embedding.
  juce::MidiMessageSequence sequence;  // Seconds, including tempo changes.
  juce::String taskId, midiTaskId, audioUrl;
  int noteCount = 0;
  double durationSeconds = 0.0;
};

// One token per job. Cancellation also interrupts a pending native HTTP read.
class Cancellation {
 public:
  void cancel();
  bool isCancelled() const { return cancelled.load(); }
  bool wait(int milliseconds);
  void setStream(std::shared_ptr<juce::WebInputStream>);

 private:
  std::atomic<bool> cancelled{false};
  juce::WaitableEvent wake;
  std::mutex mutex;
  std::shared_ptr<juce::WebInputStream> stream;
};

struct HttpRequest {
  juce::URL url;
  juce::String method{"GET"}, headers;
};

struct HttpResponse {
  int status = 0;
  juce::String contentType, error, location;
  juce::MemoryBlock body;
};

struct ClientOptions {
  juce::StringArray generateHosts{"https://api.wike.cc",
                                  "https://duomiapi.com"};
  juce::StringArray midiHosts{"https://duomiapi.com", "https://api.wike.cc"};
  int pollIntervalMs = 3000;
  int maxPolls = 200;
  juce::File outputDirectory =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("DuomiSunoAIBridge");
  juce::File logFile;
};

// Synchronous API: call only from a worker. Injected transport enables offline
// tests of exactly the same request, response and MIDI parsing path as the UI.
class DuomiClient {
 public:
  using Transport =
      std::function<HttpResponse(const HttpRequest&, Cancellation&)>;
  using Progress =
      std::function<void(const juce::String&, const juce::String&)>;
  explicit DuomiClient(ClientOptions options = {}, Transport transport = {});

  juce::Result generate(const juce::String& key, const juce::String& prompt,
                        Cancellation&, GeneratedMidi&, Progress = {},
                        const juce::String& existingTaskId = {},
                        const juce::String& existingMidiTaskId = {});
  juce::Result separateStems(
      const juce::String& key, Cancellation&, StemBatch&,
      std::function<void(const juce::String&)> progress = {});
  static juce::Result parseMidi(const juce::MemoryBlock&, GeneratedMidi&);
  // Applies to generated files before publication, including binary/URL/base64.
  static juce::Result filterPercussion(juce::MemoryBlock&);
  static juce::Result notesToMidi(const juce::var&, juce::MemoryBlock&);
  static juce::String redact(juce::String text, const juce::String& key);
  static HttpResponse requestNative(const HttpRequest&, Cancellation&);

 private:
  HttpResponse request(const HttpRequest&, const juce::String&, Cancellation&);
  juce::Result waitForTask(const juce::String& key, const juce::String& taskId,
                           Cancellation&, juce::var&,
                           const std::function<void(const juce::String&)>&);
  juce::Result decodeMidi(HttpResponse, const juce::String&, Cancellation&,
                          juce::MemoryBlock&, int depth = 0);
  ClientOptions options;
  Transport transport;
};
}  // namespace duomi
