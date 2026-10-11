#pragma once

#include "DuomiClient.h"

namespace duomi {

class MidiPreviewPlayer {
 public:
  enum class State { stopped, playing, paused, finished };
  struct Snapshot {
    State state;
    double positionSeconds, durationSeconds;
  };

  MidiPreviewPlayer();
  ~MidiPreviewPlayer();  // Host must have stopped its audio callback.
  // Control/message thread only. MIDI has already been parsed on a worker.
  void load(std::shared_ptr<const GeneratedMidi>);
  void collectRetired();
  void play();
  void pause();
  void stop();
  Snapshot getSnapshot() const;

  // Host preparation with callback suspended, then audio thread only.
  void prepare(double sampleRate);
  void release();
  void render(juce::AudioBuffer<float>&);

 private:
  struct Clip;
  class PreviewSynth;
  std::unique_ptr<PreviewSynth> synth;
  std::atomic<Clip*> pending{nullptr}, retired{nullptr};
  Clip* active =
      nullptr;  // Exclusively owned by the audio thread until retired.
  std::atomic<unsigned> command{0}, appliedCommand{0};
  std::atomic<State> publishedState{State::stopped};
  std::atomic<double> publishedPosition{0.0}, publishedDuration{0.0};
  State state = State::stopped;
  double sampleRate = 0.0;
  juce::int64 position = 0;
  int nextEvent = 0, fadeSamples = 1, silenceFadeRemaining = 0;
  float gain = 0.0f, lastOutput = 0.0f, fadeOrigin = 0.0f;
  bool ending = false;

  void requestState(unsigned);
  void reset();
  void beginSilenceFade();
  void publish();
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPreviewPlayer)
};
}  // namespace duomi
