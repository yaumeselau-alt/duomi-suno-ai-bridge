#include "ChordExtractionJob.h"

#include <array>
#include <cmath>
#include <limits>

namespace duomi {
namespace {
juce::String tr(const char* s) { return juce::String::fromUTF8(s); }
struct Quality {
  const char* suffix;
  std::vector<int> intervals;
};
const std::vector<Quality> qualities{
    {"", {0, 4, 7}},        {"m", {0, 3, 7}},        {"dim", {0, 3, 6}},
    {"aug", {0, 4, 8}},     {"sus2", {0, 2, 7}},     {"sus4", {0, 5, 7}},
    {"7", {0, 4, 7, 10}},   {"maj7", {0, 4, 7, 11}}, {"m7", {0, 3, 7, 10}},
    {"m7b5", {0, 3, 6, 10}}};
struct Window {
  std::array<double, 12> weight{};
  int bass = 128;
};
}  // namespace
ChordExtractionJob::ChordExtractionJob(juce::File dir)
    : Thread("Duomi piano chords"), directory(std::move(dir)) {}
ChordExtractionJob::~ChordExtractionJob() {
  cancel();
  waitForThreadToExit(-1);
}
ChordExtractionJob::Snapshot ChordExtractionJob::getSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex);
  auto copy = state;
  copy.busy = copy.busy || isThreadRunning();
  return copy;
}
void ChordExtractionJob::clear() {
  if (isThreadRunning()) return;
  std::lock_guard<std::mutex> lock(mutex);
  state = {};
  source.reset();
}
bool ChordExtractionJob::start(std::shared_ptr<const GeneratedMidi> value,
                               int tempo, int step) {
  if (isThreadRunning() || !value) return false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    state = {};
    state.busy = true;
    state.message = tr("正在本地分析钢琴柱式和弦…");
    source = std::move(value);
    bpm = tempo;
    beats = step;
  }
  if (!startThread()) {
    std::lock_guard<std::mutex> lock(mutex);
    state.busy = false;
    state.failed = true;
    state.message = tr("无法启动和弦分析，请重试。");
    return false;
  }
  return true;
}
juce::Result ChordExtractionJob::extract(
    const GeneratedMidi& input, int bpm, int beats, juce::MemoryBlock& output,
    int& count, int& uncertain, int& skipped,
    const std::function<bool()>& cancelled) {
  output.reset();
  count = uncertain = skipped = 0;
  if (bpm < 20 || bpm > 300 || (beats != 1 && beats != 2 && beats != 4) ||
      !std::isfinite(input.durationSeconds) || input.durationSeconds <= 0 ||
      input.durationSeconds > 3600)
    return juce::Result::fail(
        tr("和弦分析要求 20–300 BPM、1/2/4 拍间隔、最长 60 分钟 MIDI。"));
  const double step = 60.0 * beats / bpm;
  const int windowCount = (int)std::ceil(input.durationSeconds / step);
  if (windowCount > 12000 || input.sequence.getNumEvents() > 400000)
    return juce::Result::fail(tr("MIDI 太长或太复杂，无法分析柱式和弦。"));
  std::vector<Window> windows((size_t)windowCount);
  int contributions = 0;
  for (const auto* event : input.sequence) {
    if (cancelled && cancelled())
      return juce::Result::fail(tr("和弦分析已取消"));
    const auto& m = event->message;
    if (!m.isNoteOn() || m.getChannel() == 10 || !event->noteOffObject)
      continue;
    const double start = m.getTimeStamp();
    const double end = event->noteOffObject->message.getTimeStamp();
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 ||
        end <= start || start >= input.durationSeconds)
      continue;
    const int first =
        juce::jlimit(0, windowCount - 1, (int)std::floor(start / step));
    const int last = juce::jlimit(
        0, windowCount - 1,
        (int)std::ceil(juce::jmin(end, input.durationSeconds) / step) - 1);
    for (int i = first; i <= last; ++i) {
      if (++contributions > 2000000)
        return juce::Result::fail(tr("重叠音符过多，请先简化 MIDI。"));
      const double overlap = juce::jmax(
          0.0, juce::jmin(end, (i + 1) * step) - juce::jmax(start, i * step));
      auto& w = windows[(size_t)i];
      w.weight[(size_t)(m.getNoteNumber() % 12)] +=
          overlap * (0.5 + m.getFloatVelocity() * 0.5);
      if (overlap > step * 0.08) w.bass = juce::jmin(w.bass, m.getNoteNumber());
    }
  }
  // Score local evidence first; only strong, complete neighbours may supply
  // missing chord tones. A silent window never acquires a chord.
  constexpr int candidateCount = 120, noChord = candidateCount;
  constexpr int stateCount = candidateCount + 1;
  constexpr double impossible = -1.0e30;
  struct Evidence {
    float score = 0, coverage = 0;
    int present = 0;
    bool complete = false, anchor = false, repaired = false, valid = false;
  };
  struct Frame {
    std::array<Evidence, candidateCount> candidates{};
    double total = 0;
  };
  std::vector<Frame> frames((size_t)windowCount);
  for (int i = 0; i < windowCount; ++i) {
    if (cancelled && cancelled())
      return juce::Result::fail(tr("和弦分析已取消"));
    const auto& w = windows[(size_t)i];
    auto& frame = frames[(size_t)i];
    for (double v : w.weight) frame.total += v;
    if (frame.total <= 1.0e-9) continue;
    double bestComplete = impossible;
    for (int c = 0; c < candidateCount; ++c) {
      auto& e = frame.candidates[(size_t)c];
      const int root = c / 10;
      const auto& tones = qualities[(size_t)(c % 10)].intervals;
      double inside = 0;
      for (int interval : tones) {
        const double value = w.weight[(size_t)((root + interval) % 12)];
        inside += value;
        if (value >= frame.total * 0.06) ++e.present;
      }
      e.coverage = (float)(inside / frame.total);
      e.complete = e.present == (int)tones.size();
      e.score = e.coverage - 0.45f * (1.0f - e.coverage) +
                ((w.bass % 12 == root) ? 0.06f : 0);
      e.valid = e.complete && e.coverage >= 0.65f;
      if (e.valid) bestComplete = juce::jmax(bestComplete, (double)e.score);
    }
    for (auto& e : frame.candidates)
      e.anchor =
          e.valid && e.coverage >= 0.82f && e.score >= bestComplete - 0.08;
  }

  // Uniform transition cost gives an O(windows * candidates) temporal path,
  // rather than O(windows * candidates squared). Clear changes still outweigh
  // the modest continuity preference; silence/unknown frames reset it.
  std::vector<std::array<juce::uint8, stateCount>> predecessors(
      (size_t)windowCount);
  std::array<double, stateCount> previous{}, current{};
  previous.fill(impossible);
  previous[noChord] = 0;
  for (int i = 0; i < windowCount; ++i) {
    if (cancelled && cancelled())
      return juce::Result::fail(tr("和弦分析已取消"));
    auto& frame = frames[(size_t)i];
    const int bestPrevious = (int)std::distance(
        previous.begin(), std::max_element(previous.begin(), previous.end()));
    current.fill(impossible);
    bool any = false;
    const bool strongLocal =
        std::any_of(frame.candidates.begin(), frame.candidates.end(),
                    [](const Evidence& e) { return e.anchor; });
    for (int c = 0; c < candidateCount; ++c) {
      auto& e = frame.candidates[(size_t)c];
      if (!e.valid && !strongLocal && frame.total > 1.0e-9 &&
          e.coverage >= 0.8f) {
        const bool left =
            i > 0 && frames[(size_t)i - 1].candidates[(size_t)c].anchor;
        const bool right = i + 1 < windowCount &&
                           frames[(size_t)i + 1].candidates[(size_t)c].anchor;
        if ((e.present >= 2 && (left || right)) ||
            (e.present == 1 && left && right)) {
          e.valid = e.repaired = true;
          e.score -= 0.08f;
        }
      }
      if (!e.valid) continue;
      any = true;
      int from = bestPrevious;
      double best = previous[(size_t)from] - (from == noChord ? 0.0 : 0.12);
      if (previous[noChord] > best) {
        from = noChord;
        best = previous[noChord];
      }
      if (previous[(size_t)c] >= best) {
        from = c;
        best = previous[(size_t)c];
      }
      current[(size_t)c] = best + e.score;
      predecessors[(size_t)i][(size_t)c] = (juce::uint8)from;
    }
    if (!any) {
      current[noChord] = previous[(size_t)bestPrevious];
      predecessors[(size_t)i][noChord] = (juce::uint8)bestPrevious;
      if (frame.total > 1.0e-9) ++skipped;
    }
    previous = current;
  }
  std::vector<int> path((size_t)windowCount);
  int state = (int)std::distance(
      previous.begin(), std::max_element(previous.begin(), previous.end()));
  for (int i = windowCount - 1; i >= 0; --i) {
    path[(size_t)i] = state;
    state = predecessors[(size_t)i][(size_t)state];
  }
  const auto isUncertain = [&](int i, int candidate) {
    const auto& frame = frames[(size_t)i];
    const auto& selected = frame.candidates[(size_t)candidate];
    double competitor = impossible;
    for (int c = 0; c < candidateCount; ++c)
      if (c != candidate && frame.candidates[(size_t)c].valid)
        competitor =
            juce::jmax(competitor, (double)frame.candidates[(size_t)c].score);
    return selected.repaired || selected.coverage < 0.82f ||
           selected.score - competitor < 0.04;
  };

  juce::MidiFile midi;
  midi.setTicksPerQuarterNote(960);
  juce::MidiMessageSequence tempo, notes;
  tempo.addEvent(
      juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000.0 / bpm)));
  tempo.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4));
  midi.addTrack(tempo);
  notes.addEvent(juce::MidiMessage::textMetaEvent(3, "Piano Block Chords"));
  notes.addEvent(juce::MidiMessage::programChange(1, 0));
  const char* names[]{"C",  "Db", "D",  "Eb", "E",  "F",
                      "Gb", "G",  "Ab", "A",  "Bb", "B"};
  for (int i = 0; i < windowCount;) {
    if (cancelled && cancelled())
      return juce::Result::fail(tr("和弦分析已取消"));
    const int chosen = path[(size_t)i];
    if (chosen == noChord) {
      ++i;
      continue;
    }
    int end = i + 1;
    bool unsure = isUncertain(i, chosen);
    while (end < windowCount && path[(size_t)end] == chosen) {
      unsure |= isUncertain(end, chosen);
      ++end;
    }
    const int root = chosen / 10, quality = chosen % 10;
    if (unsure) ++uncertain;
    auto marker = juce::MidiMessage::textMetaEvent(
        6, juce::String(names[root]) + qualities[(size_t)quality].suffix +
               (unsure ? " ?" : ""));
    const double onTick = (double)i * beats * 960;
    marker.setTimeStamp(onTick);
    notes.addEvent(marker);
    const double endSeconds = juce::jmin(end * step, input.durationSeconds);
    const double offTick =
        juce::jmax(onTick + 1, std::round(endSeconds * bpm * 16.0));
    for (int interval : qualities[(size_t)quality].intervals) {
      const int pitch = 48 + root + interval;
      auto on = juce::MidiMessage::noteOn(1, pitch, (juce::uint8)88);
      auto off = juce::MidiMessage::noteOff(1, pitch);
      on.setTimeStamp(onTick);
      off.setTimeStamp(offTick);
      notes.addEvent(on);
      notes.addEvent(off);
    }
    ++count;
    i = end;
  }
  if (count == 0)
    return juce::Result::fail(
        tr("源 MIDI "
           "中和声信息不足，无法可靠提取柱式和弦；请使用含伴奏或分解和弦的 "
           "MIDI。"));
  midi.addTrack(notes);
  juce::MemoryOutputStream stream;
  if (!midi.writeTo(stream, 1))
    return juce::Result::fail(tr("无法编码柱式和弦 MIDI"));
  output = stream.getMemoryBlock();
  return juce::Result::ok();
}
void ChordExtractionJob::run() {
  auto result = std::make_shared<GeneratedMidi>();
  juce::MemoryBlock bytes;
  int count = 0, uncertain = 0, skipped = 0;
  auto status = juce::Result::fail(tr("和弦分析失败"));
  try {
    status = extract(*source, bpm, beats, bytes, count, uncertain, skipped,
                     [this] { return threadShouldExit(); });
    if (status.wasOk()) status = DuomiClient::parseMidi(bytes, *result);
    if (status.wasOk() && !threadShouldExit()) {
      status = directory.createDirectory();
      if (status.wasOk()) {
        result->file = directory.getChildFile("Piano-Chords-" +
                                              juce::Uuid().toString() + ".mid");
        if (!result->file.replaceWithData(bytes.getData(), bytes.getSize()))
          status = juce::Result::fail(tr("无法保存钢琴和弦 MIDI"));
      }
    }
  } catch (...) {
    status = juce::Result::fail(tr("和弦分析失败，文件过大或内存不足。"));
  }
  std::lock_guard<std::mutex> lock(mutex);
  state.busy = false;
  if (threadShouldExit()) {
    if (result->file != juce::File{}) result->file.deleteFile();
    state.message = tr("和弦分析已取消");
    return;
  }
  state.failed = status.failed();
  if (status.failed())
    state.message = status.getErrorMessage();
  else {
    state.midi = result;
    state.chordCount = count;
    state.uncertainCount = uncertain;
    state.skippedCount = skipped;
    state.message = tr("钢琴柱式和弦：") + juce::String(count) +
                    tr(" 段 · 待确认 ") + juce::String(uncertain) +
                    tr(" · 跳过 ") + juce::String(skipped) +
                    tr(" 分析窗；切换试听后可拖出。");
  }
}
}  // namespace duomi