#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

#include "DuomiClient.h"

namespace duomi {
// Control methods/snapshot on message thread; prepare/release with host
// suspended; render on audio thread. Worker owns network + decoding, never the
// audio callback.
class AudioPreviewPlayer : private juce::Thread {
 public:
  enum class State { stopped, playing, paused, finished };
  struct Waveform {
    struct Peak {
      float low = 0, high = 0;
    };
    std::vector<Peak> peaks;
    double durationSeconds = 0;
  };
  struct Snapshot {
    std::shared_ptr<const Waveform> waveform;
    bool busy = false, failed = false, ready = false;
    juce::String message, source;
    juce::File dragFile;
    State state = State::stopped;
    double positionSeconds = 0, durationSeconds = 0;
  };
  explicit AudioPreviewPlayer(
      DuomiClient::Transport = {},
      juce::File exportDirectory = juce::File::getSpecialLocation(
                                       juce::File::userApplicationDataDirectory)
                                       .getChildFile("DuomiSunoAIBridge")
                                       .getChildFile("AudioExports"));
  ~AudioPreviewPlayer() override;
  bool download(juce::String url);
  bool importFile(juce::File);
  void clear();  // Message thread, only when worker is idle.
  void cancel();
  void play();
  void pause();
  void stop();
  void seek(
      double fraction);  // Normalised position, applied on the audio thread.
  void collectRetired();
  Snapshot getSnapshot() const;
  void prepare(double sampleRate);
  void release();
  void render(juce::AudioBuffer<float>&);

 protected:
  // Kept separate so thread-start failure can be exercised without exhausting
  // operating-system resources in tests.
  virtual bool startWorker() { return startThread(); }

 private:
  struct Clip {
    juce::AudioBuffer<float> samples;
    double sampleRate = 0;
    Clip* next = nullptr;
  };
  bool start(juce::String url, juce::File file);
  void run() override;
  void requestState(unsigned);
  void publish();
  static HttpResponse downloadNative(const HttpRequest&, Cancellation&);
  DuomiClient::Transport transport;
  juce::File exportDirectory;
  mutable std::mutex mutex;
  Snapshot snapshot;
  juce::String requestedUrl;
  juce::File requestedFile;
  std::shared_ptr<Cancellation> cancellation;
  std::atomic<Clip*> pending{nullptr}, retired{nullptr};
  Clip* active = nullptr;
  std::atomic<unsigned> command{0}, appliedCommand{0};
  std::atomic<double> pendingSeek{-1.0};
  std::atomic<State> publishedState{State::stopped};
  std::atomic<double> publishedPosition{0}, publishedDuration{0};
  State state = State::stopped;
  double outputRate = 0, position = 0;
  float gain = 0, last[2]{}, fadeOrigin[2]{};
  int fadeSamples = 1, fadeRemaining = 0;
  void silenceFade();
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPreviewPlayer)
};
}  // namespace duomi