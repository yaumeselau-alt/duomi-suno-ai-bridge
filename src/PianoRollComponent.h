#pragma once
#include <JuceHeader.h>

#include "DuomiClient.h"
#include "FileDragComponent.h"
#include "MusicalGrid.h"

class PianoRollComponent final : public FileDragComponent {
 public:
  explicit PianoRollComponent(
      Starter starter =
          juce::DragAndDropContainer::performExternalDragDropOfFiles)
      : FileDragComponent(std::move(starter)) {
    setName(juce::String::fromUTF8("钢琴卷帘，按住拖出 MIDI"));
  }
  void setMidi(std::shared_ptr<const duomi::GeneratedMidi> value) {
    if (value == midi) return;
    midi = std::move(value);
    setDragFile(midi ? midi->file : juce::File{});
    setMouseCursor(midi ? juce::MouseCursor::DraggingHandCursor
                        : juce::MouseCursor::NormalCursor);
    notes.clear();
    low = 127;
    high = 0;
    if (midi)
      for (int i = 0; i < midi->sequence.getNumEvents(); ++i) {
        const auto* event = midi->sequence.getEventPointer(i);
        if (event->message.isNoteOn() && event->noteOffObject) {
          const int pitch = event->message.getNoteNumber();
          notes.push_back({event->message.getTimeStamp(),
                           event->noteOffObject->message.getTimeStamp(),
                           pitch});
          low = juce::jmin(low, pitch);
          high = juce::jmax(high, pitch);
        }
      }
    low = juce::jmax(0, low - 2);
    high = juce::jmin(127, juce::jmax(low + 12, high + 2));
    repaint();
  }
  void setPosition(double value) {
    position = value;
    repaint();
  }
  void setTempo(int value) {
    value = juce::jlimit(20, 300, value);
    if (bpm != value) {
      bpm = value;
      repaint();
    }
  }
  void paint(juce::Graphics& g) override {
    g.fillAll(juce::Colour(0xff110c1b));
    if (notes.empty()) {
      g.setColour(juce::Colour(0xffc8b4df));
      g.drawFittedText(juce::String::fromUTF8("生成或打开 MIDI 后即可试听"),
                       getLocalBounds().reduced(12),
                       juce::Justification::centred, 2);
      return;
    }
    auto area = getLocalBounds().toFloat().reduced(8);
    auto caption = area.removeFromTop(20);
    g.setColour(juce::Colour(0xffc8b4df));
    g.setFont(12.0f);
    g.drawText(midi->file.getFileName() + "  |  " +
                   juce::String(midi->noteCount) +
                   juce::String::fromUTF8(" 个音符 · 小节/拍 4/4 · ") +
                   juce::String(bpm) + " BPM",
               caption, juce::Justification::centredLeft);
    const double start = MusicalGrid::pageStart(
        juce::jmin(position, juce::jmax(0.0, midi->durationSeconds - 0.001)),
        bpm);
    const double span = MusicalGrid::barSeconds(bpm) * 4;
    const auto ruler = area.removeFromTop(18);
    const float row = area.getHeight() / (high - low + 1);
    auto x = [&](double time) {
      return area.getX() +
             static_cast<float>((time - start) / span) * area.getWidth();
    };
    for (int pitch = low; pitch <= high; ++pitch) {
      g.setColour(pitch % 12 == 0 ? juce::Colour(0xff513168)
                                  : juce::Colour(0xff25182f));
      g.drawHorizontalLine(
          static_cast<int>(area.getBottom() - (pitch - low) * row), area.getX(),
          area.getRight());
    }
    for (const auto& tick :
         MusicalGrid::ticks(start, start + span, bpm, area.getWidth())) {
      const auto px = x(tick.seconds);
      g.setColour(tick.bar ? juce::Colour(0xff65427e)
                           : juce::Colour(0xff2c1b3c));
      g.drawVerticalLine(juce::roundToInt(px), area.getY(), area.getBottom());
      if (tick.label.isNotEmpty() && px < area.getRight() - 25) {
        g.setColour(juce::Colour(0xffc8b4df));
        g.drawText(
            tick.label,
            juce::Rectangle<float>(px + 2, ruler.getY(), 66, ruler.getHeight()),
            juce::Justification::left);
      }
    }
    for (const auto& note : notes) {
      if (note.end < start || note.start > start + span) continue;
      const float left = x(juce::jmax(start, note.start));
      const float right = x(juce::jmin(start + span, note.end));
      g.setColour(note.start <= position && position < note.end
                      ? juce::Colour(0xffff90e9)
                      : juce::Colour(0xffb877ff));
      g.fillRect(left, area.getBottom() - (note.pitch - low + 1) * row,
                 juce::jmax(1.0f, right - left), juce::jmax(1.0f, row - 1));
    }
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.drawVerticalLine(static_cast<int>(x(position)), area.getY(),
                       area.getBottom());
  }

 private:
  int bpm = 120;
  struct Note {
    double start, end;
    int pitch;
  };
  std::vector<Note> notes;
  std::shared_ptr<const duomi::GeneratedMidi> midi;
  double position = 0.0;
  int low = 48, high = 84;
};
