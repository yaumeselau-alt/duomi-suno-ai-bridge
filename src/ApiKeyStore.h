#pragma once
#include <juce_events/juce_events.h>

#include <mutex>
namespace duomi {
// Processor-owned worker. Disk and OS encryption never run in processBlock.
class ApiKeyStore : private juce::Thread {
 public:
  struct Snapshot {
    bool busy = true, loaded = false, saved = false, failed = false;
    juce::String key, message;
    unsigned revision = 0;
  };
  explicit ApiKeyStore(juce::File file = defaultFile());
  ~ApiKeyStore() override;
  static juce::File defaultFile();
  bool save(juce::String);
  bool forget();
  Snapshot getSnapshot() const;

 private:
  enum class Operation { load, save, forget };
  bool begin(Operation, juce::String = {});
  void run() override;
  juce::Result encrypt(const juce::String&, juce::MemoryBlock&);
  juce::Result decrypt(const juce::MemoryBlock&, juce::String&);
  juce::File file;
  mutable std::mutex mutex;
  Snapshot snapshot;
  Operation operation = Operation::load;
  juce::String requestedKey;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ApiKeyStore)
};
}  // namespace duomi