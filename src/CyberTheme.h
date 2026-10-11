#pragma once
#include <JuceHeader.h>

namespace cyber {
inline const juce::Colour background{0xff090710}, panel{0xff14101f},
    edge{0xff493067}, purple{0xffb877ff}, pink{0xffef80eb}, text{0xfff2eaff},
    muted{0xffaa97c0};

class LookAndFeel final : public juce::LookAndFeel_V4 {
 public:
  LookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextButton::buttonColourId, panel);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextEditor::backgroundColourId, panel);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, edge);
    setColour(juce::TextEditor::focusedOutlineColourId, purple);
    setColour(juce::TextEditor::highlightColourId, purple.withAlpha(0.35f));
    setColour(juce::CaretComponent::caretColourId, pink);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::outlineColourId, edge);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::arrowColourId, purple);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, edge);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
  }
  void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                            const juce::Colour& colour, bool over,
                            bool down) override {
    auto bounds = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(
        down ? edge
             : (over && b.isEnabled() ? colour.brighter(0.22f) : colour));
    g.fillRoundedRectangle(bounds, 4);
    g.setColour((over && b.isEnabled() ? purple : edge)
                    .withAlpha(b.isEnabled() ? 1.0f : 0.4f));
    g.drawRoundedRectangle(bounds, 4, 1);
    if (b.hasKeyboardFocus(true)) {
      g.setColour(pink);
      g.drawRoundedRectangle(bounds.reduced(2), 3, 1);
    }
  }
};

class TransportButton final : public juce::TextButton {
 public:
  enum class Icon { play, pause, stop };
  explicit TransportButton(Icon value) : icon(value) {}
  void paintButton(juce::Graphics& g, bool over, bool down) override {
    getLookAndFeel().drawButtonBackground(
        g, *this, findColour(juce::TextButton::buttonColourId), over, down);
    g.setColour((down ? pink : purple).withAlpha(isEnabled() ? 1.0f : 0.32f));
    const float x = getWidth() * 0.5f, y = getHeight() * 0.5f;
    if (icon == Icon::play) {
      juce::Path p;
      p.addTriangle(x - 5, y - 8, x + 8, y, x - 5, y + 8);
      g.fillPath(p);
    } else if (icon == Icon::pause) {
      g.fillRoundedRectangle(x - 7, y - 7, 5, 14, 1);
      g.fillRoundedRectangle(x + 2, y - 7, 5, 14, 1);
    } else {
      g.fillRoundedRectangle(x - 6, y - 6, 12, 12, 1);
    }
  }

 private:
  Icon icon;
};

// Double-click edits; Return/focus-loss commits; Escape cancels.
class BpmControl final : public juce::Label {
 public:
  BpmControl() {
    setName("BPM");
    setEditable(false, true, false);
    setJustificationType(juce::Justification::centred);
    setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    setColour(backgroundColourId, panel);
    setColour(outlineColourId, edge);
    setColour(textColourId, purple);
    setColour(textWhenEditingColourId, text);
    setColour(outlineWhenEditingColourId, purple);
    setTooltip(juce::String::fromUTF8("滚轮上下调整 · 双击输入 · 20–300 BPM"));
    onEditorShow = [this] {
      if (auto* editor = getCurrentTextEditor())
        editor->setInputRestrictions(3, "0123456789");
    };
    onTextChange = [this] {
      const auto input = getText().trim();
      setBpm(input.isNotEmpty() && input.containsOnly("0123456789")
                 ? input.getIntValue()
                 : bpm);
    };
    setBpm(120);
  }
  int getBpm() const { return bpm; }
  void setBpm(int value) {
    bpm = juce::jlimit(20, 300, value);
    setText(juce::String(bpm), juce::dontSendNotification);
    if (onBpmChange) onBpmChange(bpm);
  }
  void mouseWheelMove(const juce::MouseEvent&,
                      const juce::MouseWheelDetails& wheel) override {
    if (!isEnabled() || isBeingEdited() || wheel.deltaY == 0) return;
    setBpm(bpm + (wheel.deltaY > 0 ? 1 : -1));
  }
  std::function<void(int)> onBpmChange;

 private:
  int bpm = 120;
};
}  // namespace cyber