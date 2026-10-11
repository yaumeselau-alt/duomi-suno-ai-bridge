#pragma once

#include <juce_events/juce_events.h>

#include "DuomiClient.h"

namespace duomi {

// Processor-owned: closing/reopening the editor does not orphan the request.
// Snapshot is consumed on the message thread only, never in processBlock.
class GenerationJob : private juce::Thread, public juce::ChangeBroadcaster {
 public:
  struct Snapshot {
    juce::String status{juce::String::fromUTF8("就绪")}, taskId, midiTaskId,
        audioUrl;
    bool busy = false, failed = false, resumeAvailable = false;
    juce::File logFile;
    std::shared_ptr<const GeneratedMidi> midi;
  };

  explicit GenerationJob(ClientOptions = {}, DuomiClient::Transport = {});
  ~GenerationJob() override;
  bool start(juce::String key, juce::String prompt, bool resume = false);
  bool importMidi(juce::File);
  bool restore(Snapshot metadata, juce::MemoryBlock bytes, juce::String name);
  void cancel();
  Snapshot getSnapshot() const;

 private:
  void run() override;
  mutable std::mutex mutex;
  Snapshot state;
  ClientOptions clientOptions;
  DuomiClient::Transport transport;
  std::shared_ptr<Cancellation> cancellation;
  juce::File fileToImport;
  juce::MemoryBlock restoredBytes;
  juce::String restoredName;
  juce::String apiKey, description, resumeTaskId, resumeMidiTaskId,
      resumeAudioUrl;
};
}  // namespace duomi
