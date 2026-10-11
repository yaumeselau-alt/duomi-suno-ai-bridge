#pragma once
#include <juce_core/juce_core.h>
namespace duomi {
inline juce::String generationPrompt(const juce::String& description, int bpm) {
  const auto text = description.trim();
  // Empty descriptions must fail validation before any paid request.
  return text.isEmpty()
             ? juce::String{}
             : text + ", " + juce::String(juce::jlimit(20, 300, bpm)) + " BPM";
}
}  // namespace duomi