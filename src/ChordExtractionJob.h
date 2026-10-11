#pragma once
#include <juce_events/juce_events.h>

#include "DuomiClient.h"

namespace duomi {
// Local harmonic reduction. It estimates accompaniment; it does not claim to
// recover a unique original harmony from an arbitrary melody.
class ChordExtractionJob : private juce::Thread {
 public:
  struct Snapshot {
    bool busy = false, failed = false;
    int chordCount = 0, uncertainCount = 0, skippedCount = 0;
    juce::String message;
    std::shared_ptr<const GeneratedMidi> midi;
  };
  explicit ChordExtractionJob(
      juce::File directory = juce::File::getSpecialLocation(
                                 juce::File::userApplicationDataDirectory)
                                 .getChildFile("DuomiSunoAIBridge")
                                 .getChildFile("ChordExports"));
  ~ChordExtractionJob() override;
  bool start(std::shared_ptr<const GeneratedMidi>, int bpm, int beats);
  void cancel() { signalThreadShouldExit(); }
  Snapshot getSnapshot() const;
  void clear();  // Only when idle.
  static juce::Result extract(const GeneratedMidi&, int bpm, int beats,
                              juce::MemoryBlock&, int& chords, int& uncertain,
                              int& skipped,
                              const std::function<bool()>& cancelled = {});

 private:
  void run() override;
  juce::File directory;
  mutable std::mutex mutex;
  Snapshot state;
  std::shared_ptr<const GeneratedMidi> source;
  int bpm = 120, beats = 2;
};
}  // namespace duomi