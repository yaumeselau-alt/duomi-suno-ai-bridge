#pragma once
#include <JuceHeader.h>

#include <mutex>

namespace duomi {
// Processor-owned: a closing editor never leaves a detached file worker behind.
class MidiExportJob final : private juce::Thread,
                            public juce::ChangeBroadcaster {
 public:
  struct Snapshot {
    bool busy = false, failed = false;
    juce::String message;
    juce::File file;
  };
  MidiExportJob() : Thread("MIDI export") {}
  ~MidiExportJob() override {
    signalThreadShouldExit();
    waitForThreadToExit();
  }
  bool start(juce::File source, juce::File destination);
  Snapshot getSnapshot() const;

 private:
  void run() override;
  juce::Result copyMidi();
  mutable std::mutex mutex;
  Snapshot state;
  juce::File input, output;
};
}  // namespace duomi
