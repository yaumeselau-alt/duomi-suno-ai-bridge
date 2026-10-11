#pragma once
#include <JuceHeader.h>

// A click is not a drag. Capture the file at mouse-down, then start one native
// copy operation per gesture. Completion does not imply the DAW accepted it.
class MidiDragButton final : public juce::TextButton {
 public:
  using Starter = std::function<bool(const juce::StringArray&, bool,
                                     juce::Component*, std::function<void()>)>;
  explicit MidiDragButton(
      Starter starter =
          juce::DragAndDropContainer::performExternalDragDropOfFiles)
      : startNativeDrag(std::move(starter)) {
    setButtonText(juce::String::fromUTF8("拖 MIDI 到 DAW"));
    setTooltip(juce::String::fromUTF8(
        "按住并拖到 DAW 的 MIDI 轨道；若宿主不接受，可使用导出 MIDI"));
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setEnabled(false);
  }
  void setMidiFile(juce::File file) {
    currentFile = std::move(file);
    setEnabled(currentFile != juce::File{} &&
               currentFile.hasFileExtension("mid;midi") && !inProgress);
  }
  std::function<void(const juce::String&, bool)> onStatus;
  void mouseDown(const juce::MouseEvent& event) override {
    juce::TextButton::mouseDown(event);
    attempted = false;
    capturedFile = isEnabled() && event.mods.isLeftButtonDown() ? currentFile
                                                                : juce::File{};
  }
  void mouseDrag(const juce::MouseEvent& event) override {
    juce::TextButton::mouseDrag(event);
    if (!isEnabled() || inProgress || attempted ||
        !event.mods.isLeftButtonDown() ||
        event.getDistanceFromDragStart() < 6 || capturedFile == juce::File{})
      return;
    attempted = true;
    if (!capturedFile.existsAsFile()) {
      if (onStatus)
        onStatus(
            juce::String::fromUTF8("MIDI 文件已不存在，请重新生成或打开文件。"),
            true);
      return;
    }
    inProgress = true;
    setEnabled(false);
    const juce::Component::SafePointer<MidiDragButton> safe(this);
    if (onStatus)
      onStatus(juce::String::fromUTF8("拖到 DAW 的 MIDI 轨道后松开鼠标。"),
               false);
    const bool started =
        startNativeDrag({capturedFile.getFullPathName()}, false, this, [safe] {
          if (!safe) return;
          safe->inProgress = false;
          safe->setMidiFile(safe->currentFile);
          if (safe->onStatus)
            safe->onStatus(
                juce::String::fromUTF8(
                    "拖拽已结束；请在 DAW 中确认，或使用“导出 MIDI”。"),
                false);
        });
    if (safe && !started) {
      safe->inProgress = false;
      safe->setMidiFile(safe->currentFile);
      if (safe->onStatus)
        safe->onStatus(
            juce::String::fromUTF8("无法启动系统拖拽，请使用“导出 MIDI”。"),
            true);
    }
  }

 private:
  Starter startNativeDrag;
  juce::File currentFile, capturedFile;
  bool attempted = false, inProgress = false;
};
