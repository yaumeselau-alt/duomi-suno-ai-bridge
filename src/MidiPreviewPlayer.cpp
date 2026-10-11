#include "MidiPreviewPlayer.h"

#include <cmath>

namespace duomi {
namespace {
class PreviewSound final : public juce::SynthesiserSound {
 public:
  bool appliesToNote(int) override { return true; }
  bool appliesToChannel(int) override { return true; }
};

class PreviewVoice final : public juce::SynthesiserVoice {
 public:
  bool canPlaySound(juce::SynthesiserSound* sound) override {
    return dynamic_cast<PreviewSound*>(sound) != nullptr;
  }
  void startNote(int note, float velocity, juce::SynthesiserSound*,
                 int wheel) override {
    phase = 0.0;
    frequency = juce::MidiMessage::getMidiNoteInHertz(note);
    level = velocity * 0.12f;
    envelope.setSampleRate(getSampleRate());
    envelope.setParameters({0.008f, 0.18f, 0.55f, 0.12f});
    envelope.reset();
    pitchWheelMoved(wheel);
    envelope.noteOn();
  }
  void stopNote(float, bool tail) override {
    if (tail)
      envelope.noteOff();
    else {
      envelope.reset();
      clearCurrentNote();
    }
  }
  void pitchWheelMoved(int wheel) override {
    const auto semitones = (wheel - 8192) * (2.0 / 8192.0);
    increment = juce::MathConstants<double>::twoPi * frequency *
                std::pow(2.0, semitones / 12.0) / getSampleRate();
  }
  void controllerMoved(int, int) override {}
  void renderNextBlock(juce::AudioBuffer<float>& buffer, int start,
                       int count) override {
    for (int i = start; i < start + count; ++i) {
      // A quiet, rounded tone with a little second harmonic. No external
      // assets.
      const auto sample =
          static_cast<float>(std::sin(phase) + 0.2 * std::sin(phase * 2.0)) *
          level * envelope.getNextSample();
      for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.addSample(channel, i, sample);
      phase += increment;
      if (phase >= juce::MathConstants<double>::twoPi)
        phase = std::fmod(phase, juce::MathConstants<double>::twoPi);
      if (!envelope.isActive()) {
        clearCurrentNote();
        break;
      }
    }
  }

 private:
  juce::ADSR envelope;
  double frequency = 440.0, phase = 0.0, increment = 0.0;
  float level = 0.0f;
};
}  // namespace

// All synth access is confined to the audio thread (or suspended preparation).
// JUCE note handling uses uncontended internal locks; no UI/worker touches
// them.
class MidiPreviewPlayer::PreviewSynth final : public juce::Synthesiser {
 public:
  using juce::Synthesiser::handleMidiEvent;
  using juce::Synthesiser::renderVoices;
  bool anyVoiceActive() const {
    for (auto* voice : voices)
      if (voice->isVoiceActive()) return true;
    return false;
  }
  void resetChannels() {
    allNotesOff(0, false);
    for (int channel = 1; channel <= 16; ++channel) {
      handleController(channel, 64, 0);
      handleController(channel, 66, 0);
      handlePitchWheel(channel, 8192);
    }
  }
  void finishNotes() {
    for (int channel = 1; channel <= 16; ++channel) {
      handleController(channel, 64, 0);
      handleController(channel, 66, 0);
    }
    allNotesOff(0, true);
  }

 protected:
  // Avoid the default steal-list allocation on the first over-capacity chord.
  juce::SynthesiserVoice* findVoiceToSteal(juce::SynthesiserSound*, int,
                                           int) const override {
    juce::SynthesiserVoice* oldest = nullptr;
    for (auto* voice : voices)
      if (oldest == nullptr || voice->wasStartedBefore(*oldest)) oldest = voice;
    return oldest;
  }
};

struct MidiPreviewPlayer::Clip {
  explicit Clip(std::shared_ptr<const GeneratedMidi> value)
      : midi(std::move(value)) {}
  std::shared_ptr<const GeneratedMidi> midi;
};

MidiPreviewPlayer::MidiPreviewPlayer()
    : synth(std::make_unique<PreviewSynth>()) {
  static_assert(std::atomic<Clip*>::is_always_lock_free);
  static_assert(std::atomic<double>::is_always_lock_free);
  for (int i = 0; i < 32; ++i) synth->addVoice(new PreviewVoice);
  synth->addSound(new PreviewSound);
  synth->setNoteStealingEnabled(true);
}
MidiPreviewPlayer::~MidiPreviewPlayer() {
  delete pending.exchange(nullptr);
  delete retired.exchange(nullptr);
  delete active;
}

void MidiPreviewPlayer::load(std::shared_ptr<const GeneratedMidi> midi) {
  collectRetired();
  auto clip = std::make_unique<Clip>(std::move(midi));
  stop();
  // Atomic ownership transfer; unused pending data is reclaimed on this thread.
  delete pending.exchange(clip.release());
}
void MidiPreviewPlayer::collectRetired() { delete retired.exchange(nullptr); }
void MidiPreviewPlayer::requestState(unsigned requested) {
  auto old = command.load();
  for (;;) {
    const auto resetFlag = old != appliedCommand.load() ? old & 4u : 0u;
    const auto next = ((old + 8u) & ~7u) | resetFlag | requested;
    if (command.compare_exchange_weak(old, next)) break;
  }
}
void MidiPreviewPlayer::play() { requestState(1); }
void MidiPreviewPlayer::pause() { requestState(2); }
void MidiPreviewPlayer::stop() { requestState(4); }

MidiPreviewPlayer::Snapshot MidiPreviewPlayer::getSnapshot() const {
  return {publishedState.load(), publishedPosition.load(),
          publishedDuration.load()};
}
void MidiPreviewPlayer::publish() {
  publishedState.store(state);
  publishedPosition.store(
      sampleRate > 0.0
          ? juce::jmin(position / sampleRate, publishedDuration.load())
          : 0.0);
}
void MidiPreviewPlayer::beginSilenceFade() {
  fadeOrigin = lastOutput;
  silenceFadeRemaining = fadeSamples;
  gain = 0.0f;
}
void MidiPreviewPlayer::reset() {
  synth->resetChannels();
  position = 0;
  nextEvent = 0;
  ending = false;
  state = State::stopped;
  beginSilenceFade();
}
void MidiPreviewPlayer::prepare(double rate) {
  sampleRate = std::isfinite(rate) && rate > 0.0 ? rate : 0.0;
  if (sampleRate > 0.0) synth->setCurrentPlaybackSampleRate(sampleRate);
  fadeSamples = juce::jmax(1, static_cast<int>(sampleRate * 0.005));
  lastOutput = 0.0f;
  reset();
  appliedCommand.store(command.load());
  publish();
}
void MidiPreviewPlayer::release() {
  reset();
  sampleRate = 0.0;
  publish();
}

void MidiPreviewPlayer::render(juce::AudioBuffer<float>& buffer) {
  buffer.clear();
  const int count = buffer.getNumSamples();
  if (count <= 0 || buffer.getNumChannels() == 0 || sampleRate <= 0.0) return;
  // Only replace if the previous retired clip has been reclaimed. The audio
  // thread never deletes a sequence or decrements a shared_ptr reference count.
  bool adopted = false;
  if (retired.load() == nullptr) {
    if (auto* next = pending.exchange(nullptr)) {
      auto* previous = active;
      active = next;
      reset();
      publishedDuration.store(active->midi ? active->midi->durationSeconds
                                           : 0.0);
      retired.store(previous);
      adopted = true;
    }
  }
  const auto latest = command.load();
  const auto control =
      adopted || latest != appliedCommand.load() ? latest & 7u : 0u;
  appliedCommand.store(latest);
  if ((control & 4u) != 0) reset();
  if ((control & 3u) == 1 && active && active->midi &&
      active->midi->noteCount > 0) {
    if (state == State::finished) reset();
    if (state != State::playing) {
      gain = 0.0f;
      silenceFadeRemaining = 0;
    }
    state = State::playing;
  } else if ((control & 3u) == 2 && state == State::playing) {
    state = State::paused;
    beginSilenceFade();  // Voices and playhead freeze; resuming retains held
                         // notes.
  }

  if (state == State::playing && active && active->midi) {
    const auto& sequence = active->midi->sequence;
    const auto blockEnd = position + count;
    int rendered = 0;
    while (nextEvent < sequence.getNumEvents()) {
      const auto& message = sequence.getEventPointer(nextEvent)->message;
      const auto eventSample = static_cast<juce::int64>(
          std::llround(message.getTimeStamp() * sampleRate));
      if (eventSample >= blockEnd) break;
      const int offset = static_cast<int>(
          juce::jlimit<juce::int64>(0, count, eventSample - position));
      if (offset > rendered)
        synth->renderVoices(buffer, rendered, offset - rendered);
      rendered = offset;
      synth->handleMidiEvent(message);
      ++nextEvent;
    }
    if (nextEvent == sequence.getNumEvents() && !ending) {
      synth->finishNotes();
      ending = true;
    }
    if (rendered < count)
      synth->renderVoices(buffer, rendered, count - rendered);
    position = blockEnd;
    for (int i = 0; i < count; ++i) {
      gain = juce::jmin(1.0f, gain + 1.0f / fadeSamples);
      for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.setSample(channel, i,
                         std::tanh(buffer.getSample(channel, i)) * gain);
    }
    if (ending && !synth->anyVoiceActive()) state = State::finished;
    lastOutput = buffer.getSample(0, count - 1);
  } else {
    for (int i = 0; i < count && silenceFadeRemaining > 0; ++i) {
      const auto value =
          fadeOrigin * static_cast<float>(--silenceFadeRemaining) / fadeSamples;
      for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.setSample(channel, i, value);
    }
    lastOutput = buffer.getSample(0, count - 1);
  }
  publish();
}
}  // namespace duomi
