#pragma once
#include <JuceHeader.h>
// One copy-only OS file drag per left-button gesture. Source files are
// retained.
class FileDragComponent : public juce::Component {
 public:
  using Starter = std::function<bool(const juce::StringArray&, bool,
                                     juce::Component*, std::function<void()>)>;
  explicit FileDragComponent(
      Starter starter =
          juce::DragAndDropContainer::performExternalDragDropOfFiles)
      : startNativeDrag(std::move(starter)) {}
  std::function<void(const juce::String&, bool)> onStatus;
  void setDragFile(juce::File file) { currentFile = std::move(file); }
  void mouseDown(const juce::MouseEvent& event) override {
    attempted = false;
    capturedFile = isEnabled() && !inProgress && event.mods.isLeftButtonDown()
                       ? currentFile
                       : juce::File{};
  }
  void mouseDrag(const juce::MouseEvent& event) override {
    if (attempted || inProgress || !isEnabled() ||
        !event.mods.isLeftButtonDown() || event.getDistanceFromDragStart() < 6)
      return;
    attempted = true;
    if (capturedFile == juce::File{} || !capturedFile.existsAsFile()) {
      if (onStatus)
        onStatus(juce::String::fromUTF8("没有可拖出的文件，请先生成或打开文件；"
                                        "若缓存已删除，请重新加载。"),
                 true);
      return;
    }
    inProgress = true;
    const juce::Component::SafePointer<FileDragComponent> safe(this);
    if (onStatus)
      onStatus(juce::String::fromUTF8("拖到对应的 MIDI 或音频轨道后松开鼠标。"),
               false);
    const bool started =
        startNativeDrag({capturedFile.getFullPathName()}, false, this, [safe] {
          if (!safe) return;
          safe->inProgress = false;
          if (safe->onStatus)
            safe->onStatus(
                juce::String::fromUTF8("拖拽已结束，请在 DAW "
                                       "中确认片段；可通过文件目录手动拖入。"),
                false);
        });
    if (safe && !started) {
      safe->inProgress = false;
      if (safe->onStatus)
        safe->onStatus(juce::String::fromUTF8(
                           "无法启动系统拖拽，可打开文件目录后手动拖入。"),
                       true);
    }
  }

 protected:
  bool dragAttempted() const { return attempted || inProgress; }

 private:
  Starter startNativeDrag;
  juce::File currentFile, capturedFile;
  bool attempted = false, inProgress = false;
};