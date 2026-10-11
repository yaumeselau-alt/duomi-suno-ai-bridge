#pragma once
#include "DuomiClient.h"
namespace duomi {
class AudioStemJob : private juce::Thread {
 public:
  struct Snapshot {
    bool busy = false, failed = false;
    juce::String message;
    StemBatch batch;
  };
  static juce::File defaultCacheDirectory() {
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("DuomiSunoAIBridge")
        .getChildFile("StemTasks");
  }
  static juce::String cacheName(const juce::String& task) {
    return "stems-" + juce::String::toHexString(task.hashCode64()) + ".json";
  }
  explicit AudioStemJob(ClientOptions o = {}, DuomiClient::Transport t = {},
                        juce::File directory = defaultCacheDirectory())
      : Thread("Duomi audio stems"),
        options(std::move(o)),
        transport(std::move(t)),
        cacheDirectory(std::move(directory)) {}
  ~AudioStemJob() {
    cancel();
    waitForThreadToExit(-1);
  }
  Snapshot getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    auto s = state;
    s.busy |= isThreadRunning();
    return s;
  }
  bool start(juce::String key, juce::String sourceTask);
  void cancel() {
    std::shared_ptr<Cancellation> c;
    {
      std::lock_guard<std::mutex> lock(mutex);
      c = token;
    }
    if (c) c->cancel();
  }
  void restore(StemBatch b) {
    if (isThreadRunning()) return;
    std::lock_guard<std::mutex> lock(mutex);
    state = {};
    state.batch = std::move(b);
  }
  void cache(int index, const juce::File& f) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.busy && index > 0 && index <= (int)state.batch.stems.size())
      state.batch.stems[(size_t)index - 1].file = f.getFullPathName();
  }

 private:
  void run() override;
  mutable std::mutex mutex;
  Snapshot state;
  ClientOptions options;
  DuomiClient::Transport transport;
  std::shared_ptr<Cancellation> token;
  juce::String key;
  juce::File cacheDirectory;
};
}  // namespace duomi