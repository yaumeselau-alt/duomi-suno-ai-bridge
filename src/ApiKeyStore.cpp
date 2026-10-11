#include "ApiKeyStore.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <wincrypt.h>
// clang-format on
#endif
namespace duomi {
namespace {
juce::String tr(const char* text) { return juce::String::fromUTF8(text); }
bool validKey(const juce::String& key) {
  if (key.isEmpty() || key.length() > 4096) return false;
  for (auto ch : key)
    if (ch < 33 || ch > 126) return false;
  return true;
}
}  // namespace
juce::File ApiKeyStore::defaultFile() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("DuomiSunoAIBridge")
      .getChildFile("api-key.dpapi");
}
ApiKeyStore::ApiKeyStore(juce::File destination)
    : Thread("Duomi key storage"), file(std::move(destination)) {
  snapshot.message = tr("正在读取本机 Key…");
  if (!startThread()) {
    std::lock_guard<std::mutex> lock(mutex);
    snapshot.busy = false;
    snapshot.loaded = true;
    snapshot.failed = true;
    snapshot.message = tr("无法启动 Key 存储线程，请重新打开插件。");
    requestedKey.clear();
  }
}
ApiKeyStore::~ApiKeyStore() { waitForThreadToExit(-1); }
ApiKeyStore::Snapshot ApiKeyStore::getSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex);
  auto result = snapshot;
  result.busy |= isThreadRunning();
  return result;
}
bool ApiKeyStore::save(juce::String key) {
  return begin(Operation::save, key.trim());
}
bool ApiKeyStore::forget() { return begin(Operation::forget); }
bool ApiKeyStore::begin(Operation next, juce::String key) {
  if (isThreadRunning()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (snapshot.busy) return false;
    if (next == Operation::save && !validKey(key)) {
      snapshot.failed = true;
      snapshot.message = tr("Key 为空或含无效字符，请重新输入。");
      return false;
    }
    operation = next;
    requestedKey = std::move(key);
    snapshot.busy = true;
    snapshot.failed = false;
    snapshot.message = next == Operation::save ? tr("正在加密保存…")
                                               : tr("正在移除已保存 Key…");
  }
  if (!startThread()) {
    std::lock_guard<std::mutex> lock(mutex);
    snapshot.busy = false;
    snapshot.loaded = true;
    snapshot.failed = true;
    snapshot.message = tr("无法启动 Key 存储线程，请重新打开插件。");
    requestedKey.clear();
    return false;
  }
  return true;
}
juce::Result ApiKeyStore::encrypt(const juce::String& key,
                                  juce::MemoryBlock& output) {
#if JUCE_WINDOWS
  DATA_BLOB input{static_cast<DWORD>(key.getNumBytesAsUTF8()),
                  reinterpret_cast<BYTE*>(const_cast<char*>(key.toRawUTF8()))},
      encrypted{};
  if (!CryptProtectData(&input, L"Duomi API Key", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
    return juce::Result::fail(tr("Windows 无法加密 Key，请稍后重试。"));
  output.append(encrypted.pbData, encrypted.cbData);
  LocalFree(encrypted.pbData);
  return juce::Result::ok();
#else
  juce::ignoreUnused(key, output);
  return juce::Result::fail(
      tr("本地 Key 保存暂仅支持 Windows；可手动输入使用。"));
#endif
}
juce::Result ApiKeyStore::decrypt(const juce::MemoryBlock& input,
                                  juce::String& key) {
#if JUCE_WINDOWS
  DATA_BLOB encrypted{
      static_cast<DWORD>(input.getSize()),
      reinterpret_cast<BYTE*>(const_cast<void*>(input.getData()))},
      plain{};
  if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &plain))
    return juce::Result::fail(
        tr("已保存 Key 损坏或属于其他账户，请重新输入并保存。"));
  key = juce::String::fromUTF8(reinterpret_cast<const char*>(plain.pbData),
                               static_cast<int>(plain.cbData));
  SecureZeroMemory(plain.pbData, plain.cbData);
  LocalFree(plain.pbData);
  if (!validKey(key)) {
    key.clear();
    return juce::Result::fail(tr("已保存 Key 无效，请重新输入。"));
  }
  return juce::Result::ok();
#else
  juce::ignoreUnused(input, key);
  return juce::Result::fail(tr("当前平台无法读取 Windows 加密 Key。"));
#endif
}
void ApiKeyStore::run() {
  auto result = juce::Result::ok();
  juce::String loaded;
  bool saved = false;
  try {
    if (operation == Operation::load) {
      if (file.exists()) {
        juce::MemoryBlock bytes;
        if (!file.existsAsFile() || file.getSize() <= 0 ||
            file.getSize() > 65536 || !file.loadFileAsData(bytes))
          result = juce::Result::fail(
              tr("无法读取本机 Key 文件，请重新输入或忘记旧 Key。"));
        else {
          result = decrypt(bytes, loaded);
          saved = result.wasOk();
        }
      }
    } else if (operation == Operation::save) {
      juce::MemoryBlock bytes;
      result = encrypt(requestedKey, bytes);
      if (result.wasOk()) result = file.getParentDirectory().createDirectory();
      if (result.wasOk()) {
        juce::TemporaryFile temporary(file);
        if (!temporary.getFile().replaceWithData(bytes.getData(),
                                                 bytes.getSize()) ||
            !temporary.overwriteTargetFileWithTemporary())
          result = juce::Result::fail(
              tr("保存 Key 失败，请检查目录权限或磁盘空间。"));
        else {
          loaded = requestedKey;
          saved = true;
        }
      } else
        result = juce::Result::fail(
            tr("无法加密或保存 Key，请检查系统和目录权限。"));
    } else {
      if (file.exists() && (!file.existsAsFile() || !file.deleteFile()))
        result = juce::Result::fail(tr("无法移除已保存 Key，请检查文件权限。"));
    }
  } catch (...) {
    result = juce::Result::fail(tr("本机 Key 操作失败，请重试。"));
  }
  std::lock_guard<std::mutex> lock(mutex);
  snapshot.busy = false;
  snapshot.loaded = true;
  snapshot.failed = result.failed();
  if (result.wasOk()) {
    snapshot.key = std::move(loaded);
    snapshot.saved = saved;
    ++snapshot.revision;
    snapshot.message =
        saved ? tr("已加密保存在本机") : tr("未保存 Key，仅本次使用");
  } else
    snapshot.message = result.getErrorMessage();
  requestedKey.clear();
}
}  // namespace duomi