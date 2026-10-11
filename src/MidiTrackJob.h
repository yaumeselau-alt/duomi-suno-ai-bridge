#pragma once
#include "DuomiClient.h"
namespace duomi {
class MidiTrackJob : private juce::Thread {
 public:
  struct Snapshot {
    bool busy = false, failed = false;
    int index = 0;
    juce::String message;
    std::shared_ptr<const GeneratedMidi> source, midi;
  };
  MidiTrackJob() : Thread("Duomi MIDI track") {}
  ~MidiTrackJob() {
    cancel();
    waitForThreadToExit(-1);
  }
  void cancel() { signalThreadShouldExit(); }
  Snapshot getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    auto s = state;
    s.busy |= isThreadRunning();
    return s;
  }
  bool start(std::shared_ptr<const GeneratedMidi> source, int index);
  static juce::Result extract(const GeneratedMidi&, int index,
                              juce::MemoryBlock&);

 private:
  void run() override;
  mutable std::mutex mutex;
  Snapshot state;
};
}  // namespace duomi