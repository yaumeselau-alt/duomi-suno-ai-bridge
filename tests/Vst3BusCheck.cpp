#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
struct MemoryStream final : IBStream {
  std::vector<char> bytes;
  int64 position = 0;
  uint32 refs = 1;
  tresult PLUGIN_API queryInterface(const TUID id, void** result) override {
    if (!result) return kInvalidArgument;
    if (std::memcmp(id, IBStream_iid, 16) == 0 ||
        std::memcmp(id, FUnknown_iid, 16) == 0) {
      *result = static_cast<IBStream*>(this);
      addRef();
      return kResultOk;
    }
    *result = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return ++refs; }
  uint32 PLUGIN_API release() override {
    return --refs;
  }  // Harness owns lifetime.
  tresult PLUGIN_API read(void* buffer, int32 count, int32* done) override {
    if (done) *done = 0;
    if (count < 0 || (!buffer && count > 0)) return kInvalidArgument;
    auto available = std::max<int64>(0, (int64)bytes.size() - position);
    const auto n = (int32)std::min<int64>(count, available);
    if (n > 0) std::memcpy(buffer, bytes.data() + position, (size_t)n);
    position += n;
    if (done) *done = n;
    return kResultOk;
  }
  tresult PLUGIN_API write(void* buffer, int32 count, int32* done) override {
    if (done) *done = 0;
    if (count < 0 || (!buffer && count > 0) ||
        position + count > 32 * 1024 * 1024)
      return kInvalidArgument;
    if (position + count > (int64)bytes.size())
      bytes.resize((size_t)(position + count));
    if (count > 0) std::memcpy(bytes.data() + position, buffer, (size_t)count);
    position += count;
    if (done) *done = count;
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 offset, int32 mode, int64* result) override {
    const auto base =
        mode == kIBSeekSet
            ? 0
            : (mode == kIBSeekCur ? position : (int64)bytes.size());
    if (mode < kIBSeekSet || mode > kIBSeekEnd || offset < -base ||
        offset > 32 * 1024 * 1024 - base)
      return kInvalidArgument;
    position = base + offset;
    if (result) *result = position;
    return kResultOk;
  }
  tresult PLUGIN_API tell(int64* result) override {
    if (!result) return kInvalidArgument;
    *result = position;
    return kResultOk;
  }
};
struct LoadedPlugin {
  HMODULE module = nullptr;
  bool (*exitDll)() = nullptr;
  IPluginFactory* factory = nullptr;
  IComponent* component = nullptr;
  IAudioProcessor* processor = nullptr;
  bool initialised = false, active = false, processing = false;
  ~LoadedPlugin() {
    if (processing) processor->setProcessing(false);
    if (active) component->setActive(false);
    if (initialised) component->terminate();
    if (processor) processor->release();
    if (component) component->release();
    if (factory) factory->release();
    if (exitDll) exitDll();
    if (module) FreeLibrary(module);
  }
};
}  // namespace
int wmain(int argc, wchar_t** argv) {
  try {
    require(argc == 2, "Usage: Vst3BusCheck path-to-vst3-binary");
    LoadedPlugin plugin;
    plugin.module = LoadLibraryW(argv[1]);
    require(plugin.module != nullptr, "LoadLibrary failed");
    auto initDll =
        reinterpret_cast<bool (*)()>(GetProcAddress(plugin.module, "InitDll"));
    auto getFactory = reinterpret_cast<IPluginFactory*(PLUGIN_API*)()>(
        GetProcAddress(plugin.module, "GetPluginFactory"));
    require(initDll && getFactory && initDll(),
            "VST3 module initialisation failed");
    plugin.exitDll =
        reinterpret_cast<bool (*)()>(GetProcAddress(plugin.module, "ExitDll"));
    plugin.factory = getFactory();
    require(plugin.factory != nullptr, "Missing VST3 factory");
    for (int i = 0; i < plugin.factory->countClasses(); ++i) {
      PClassInfo info{};
      require(plugin.factory->getClassInfo(i, &info) == kResultOk,
              "Cannot enumerate VST3 classes");
      if (std::strcmp(info.category, kVstAudioEffectClass) != 0) continue;
      require(plugin.factory->createInstance(
                  info.cid, IComponent_iid,
                  reinterpret_cast<void**>(&plugin.component)) == kResultOk,
              "Cannot instantiate VST3 component");
      break;
    }
    require(plugin.component != nullptr, "Missing audio component");
    require(plugin.component->initialize(nullptr) == kResultOk,
            "Component initialise failed");
    plugin.initialised = true;
    MemoryStream saved, roundTrip;
    require(
        plugin.component->getState(&saved) == kResultOk && !saved.bytes.empty(),
        "VST3 did not expose processor session state");
    const std::string serialized(saved.bytes.begin(), saved.bytes.end());
    require(serialized.find("DuomiSunoAIBridge") != std::string::npos &&
                serialized.find("chordBeats") != std::string::npos,
            "Session payload missing from actual VST3 getState");
    saved.position = 0;
    require(plugin.component->setState(&saved) == kResultOk &&
                plugin.component->getState(&roundTrip) == kResultOk &&
                saved.bytes == roundTrip.bytes,
            "Actual VST3 state round trip failed");
    const auto events = plugin.component->getBusCount(kEvent, kInput);
    std::cout << "Event input buses: " << events << std::endl;
    require(events >= 1,
            "Instrument missing MIDI event input bus (Live rejects this)");
    BusInfo eventBus{};
    require(plugin.component->getBusInfo(kEvent, kInput, 0, eventBus) ==
                    kResultOk &&
                eventBus.channelCount > 0 && eventBus.busType == kMain,
            "Invalid main MIDI input bus");
    require(plugin.component->getBusCount(kAudio, kInput) == 0,
            "Unexpected audio input bus");
    require(plugin.component->getBusCount(kAudio, kOutput) == 1,
            "Missing audio output");
    require(plugin.component->queryInterface(
                IAudioProcessor_iid,
                reinterpret_cast<void**>(&plugin.processor)) == kResultOk,
            "Missing audio processor interface");
    SpeakerArrangement output = SpeakerArr::kStereo, unusedInput = 0;
    require(plugin.processor->setBusArrangements(&unusedInput, 0, &output, 1) ==
                kResultOk,
            "Stereo layout rejected");
    ProcessSetup setup{kRealtime, kSample32, 64, 48000};
    require(plugin.processor->setupProcessing(setup) == kResultOk,
            "Audio setup failed");
    require(plugin.component->activateBus(kEvent, kInput, 0, true) == kResultOk,
            "Cannot activate MIDI input");
    require(
        plugin.component->activateBus(kAudio, kOutput, 0, true) == kResultOk,
        "Cannot activate stereo output");
    require(plugin.component->setActive(true) == kResultOk,
            "Cannot activate VST3");
    plugin.active = true;
    require(plugin.processor->setProcessing(true) == kResultOk,
            "Cannot start processing");
    plugin.processing = true;
    float left[64]{}, right[64]{};
    float* channels[]{left, right};
    AudioBusBuffers buffer{};
    buffer.numChannels = 2;
    buffer.channelBuffers32 = channels;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = 64;
    data.numOutputs = 1;
    data.outputs = &buffer;
    require(plugin.processor->process(data) == kResultOk,
            "VST3 audio callback failed");
    for (int i = 0; i < 64; ++i)
      require(std::isfinite(left[i]) && std::isfinite(right[i]),
              "Invalid VST3 output");
    std::cout << "PASS actual VST3: factory, component, MIDI input, stereo "
                 "output, audio processing and session-state round trip\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << std::endl;
    return 1;
  }
}
