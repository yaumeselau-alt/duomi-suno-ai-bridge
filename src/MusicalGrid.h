#pragma once
#include <JuceHeader.h>

#include <cmath>

// Shared fixed-tempo 4/4 ruler. This is a display grid, never a MIDI quantiser
// or a claim to have inferred the source's tempo/meter.
struct MusicalGrid {
  struct Tick {
    double seconds;
    bool bar;
    juce::String label;
  };
  static double beatSeconds(int bpm) {
    return 60.0 / juce::jlimit(20, 300, bpm);
  }
  static double barSeconds(int bpm) { return beatSeconds(bpm) * 4; }
  static double pageStart(double position, int bpm) {
    const double page = barSeconds(bpm) * 4;
    return std::floor(juce::jmax(0.0, position) / page) * page;
  }
  static std::vector<Tick> ticks(double start, double end, int bpm,
                                 float width) {
    std::vector<Tick> result;
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 ||
        end <= start || width <= 0)
      return result;
    const double beat = beatSeconds(bpm), bar = beat * 4, span = end - start;
    const double barPixels = width * bar / span;
    int labelStride = 1;
    while (barPixels * labelStride < 70 && labelStride < (1 << 24))
      labelStride *= 2;
    const bool showBeats = barPixels / 4 >= 12;
    int gridStride = 1;
    while (barPixels * gridStride < 8 && gridStride < (1 << 24))
      gridStride *= 2;
    const double tickSpan = showBeats ? beat : bar * gridStride;
    const auto first = (juce::int64)std::ceil(start / tickSpan - 1.0e-8);
    for (juce::int64 index = first; result.size() < 2048; ++index) {
      const double time = index * tickSpan;
      if (time > end + 1.0e-8) break;
      const auto beatIndex = (juce::int64)std::llround(time / beat);
      const auto barIndex = beatIndex / 4;
      const bool isBar = beatIndex % 4 == 0;
      const bool label = isBar && barIndex % labelStride == 0;
      result.push_back(
          {time, isBar,
           label ? juce::String(barIndex + 1) + ".1" : juce::String{}});
    }
    return result;
  }
};