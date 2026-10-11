#include <iostream>
#include <stdexcept>

#include "ApiKeyStore.h"
#include "GenerationJob.h"
namespace {
void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void wait(duomi::ApiKeyStore& store) {
  const auto end = juce::Time::getMillisecondCounterHiRes() + 5000;
  while (store.getSnapshot().busy &&
         juce::Time::getMillisecondCounterHiRes() < end)
    juce::Thread::sleep(2);
  check(!store.getSnapshot().busy, "Key worker timed out");
  juce::Thread::sleep(5);
}
struct Folder {
  juce::File path =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("duomi-key-test-" + juce::Uuid().toString());
  Folder() { path.createDirectory(); }
  ~Folder() { path.deleteRecursively(); }
};
}  // namespace
int runApiKeyStoreTests() {
#if JUCE_WINDOWS
  int passed = 0;
  auto test = [&](const char* name, auto fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << std::endl;
  };
  test("missing store, encrypted roundtrip and reopen", [] {
    Folder folder;
    auto file = folder.path.getChildFile("key.dpapi");
    {
      duomi::ApiKeyStore store(file);
      wait(store);
      check(store.getSnapshot().loaded && !store.getSnapshot().saved &&
                !file.exists(),
            "Missing store created data");
      check(store.save("  fake-duomi-test-key-123  "), "Save refused");
      wait(store);
      check(store.getSnapshot().saved && !store.getSnapshot().failed,
            "Save failed");
      juce::MemoryBlock bytes;
      check(file.loadFileAsData(bytes), "No encrypted file");
      std::string raw(static_cast<const char*>(bytes.getData()),
                      bytes.getSize());
      check(raw.find("fake-duomi-test-key-123") == std::string::npos,
            "Plain key written");
    }
    {
      duomi::ApiKeyStore reopened(file);
      wait(reopened);
      check(reopened.getSnapshot().key == "fake-duomi-test-key-123",
            "Reopen failed");
    }
  });
  test("replace key, forget and fresh session", [] {
    Folder folder;
    auto file = folder.path.getChildFile("key.dpapi");
    {
      duomi::ApiKeyStore store(file);
      wait(store);
      store.save("fake-one");
      wait(store);
      store.save("fake-two");
      wait(store);
      check(store.getSnapshot().key == "fake-two", "Replacement failed");
      check(store.forget(), "Forget refused");
      wait(store);
      check(!file.exists() && !store.getSnapshot().saved &&
                store.getSnapshot().key.isEmpty(),
            "Forget failed");
    }
    {
      duomi::ApiKeyStore fresh(file);
      wait(fresh);
      check(fresh.getSnapshot().key.isEmpty(), "Forgotten key recovered");
    }
  });
  test("corruption, oversized file and recovery", [] {
    Folder folder;
    auto file = folder.path.getChildFile("key.dpapi");
    file.replaceWithText("broken");
    {
      duomi::ApiKeyStore store(file);
      wait(store);
      check(store.getSnapshot().failed && store.getSnapshot().key.isEmpty(),
            "Corruption accepted");
      store.save("fake-recovery");
      wait(store);
      check(!store.getSnapshot().failed && store.getSnapshot().saved,
            "Recovery failed");
    }
    juce::MemoryBlock huge(65537, true);
    file.replaceWithData(huge.getData(), huge.getSize());
    {
      duomi::ApiKeyStore store(file);
      wait(store);
      check(store.getSnapshot().failed, "Oversized file accepted");
      store.forget();
      wait(store);
      check(!file.exists(), "Corrupt file removal failed");
    }
  });
  test("invalid input preserves saved key", [] {
    Folder folder;
    auto file = folder.path.getChildFile("key.dpapi");
    duomi::ApiKeyStore store(file);
    wait(store);
    store.save("fake-original");
    wait(store);
    for (auto invalid :
         {juce::String{}, juce::String("abc\r\ndef"), juce::String("a b"),
          juce::String::repeatedString("x", 4097)}) {
      check(!store.save(invalid), "Invalid key accepted");
      check(store.getSnapshot().key == "fake-original" &&
                store.getSnapshot().saved,
            "Invalid input removed key");
    }
  });
  test("filesystem failures do not remove directories", [] {
    Folder folder;
    auto directory = folder.path.getChildFile("directory");
    directory.createDirectory();
    auto child = directory.getChildFile("keep.txt");
    child.replaceWithText("keep");
    duomi::ApiKeyStore store(directory);
    wait(store);
    check(store.getSnapshot().failed, "Directory accepted as key");
    store.save("fake-key");
    wait(store);
    check(store.getSnapshot().failed, "Directory overwritten");
    store.forget();
    wait(store);
    check(store.getSnapshot().failed && child.existsAsFile(),
          "Forget removed directory");
    auto parent = folder.path.getChildFile("file-parent");
    parent.replaceWithText("keep");
    duomi::ApiKeyStore blocked(parent.getChildFile("key.dpapi"));
    wait(blocked);
    blocked.save("fake-key");
    wait(blocked);
    check(blocked.getSnapshot().failed && !blocked.getSnapshot().saved,
          "Write failure reported success");
  });
  test("destruction joins pending save", [] {
    Folder folder;
    auto file = folder.path.getChildFile("key.dpapi");
    {
      duomi::ApiKeyStore store(file);
      wait(store);
      check(store.save("fake-close"), "Save rejected after completed load");
    }
    {
      duomi::ApiKeyStore store(file);
      wait(store);
      check(store.getSnapshot().key == "fake-close", "Save orphaned on close");
    }
  });
  test("input errors are specific and never send requests", [] {
    int calls = 0;
    duomi::GenerationJob job({}, [&](const auto&, auto&) {
      ++calls;
      return duomi::HttpResponse{};
    });
    check(!job.start("", "piano"), "Empty key accepted");
    auto keyError = job.getSnapshot().status;
    check(!job.start("fake-key", ""), "Empty prompt accepted");
    check(job.getSnapshot().status != keyError, "Input errors not specific");
    check(!job.start("fake-key", "", true), "Missing resume task accepted");
    check(calls == 0, "Validation called network");
  });
  std::cout << passed << " key storage/error groups passed\n";
#endif
  return 0;
}