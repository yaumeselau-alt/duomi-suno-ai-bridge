#pragma once
#include <JuceHeader.h>

#include "AudioPreviewPlayer.h"
#include "FileDragComponent.h"
#include "MusicalGrid.h"
#include "Localisation.h"

class AudioWaveformComponent final : public FileDragComponent {
 public:
  explicit AudioWaveformComponent(
      Starter starter =
          juce::DragAndDropContainer::performExternalDragDropOfFiles)
      : FileDragComponent(std::move(starter)) {
    setName(tr("音频波形进度条"));
  }
  std::function<void(double)> onSeek;
  void setPreview(const duomi::AudioPreviewPlayer::Snapshot& value) {
    waveform = value.waveform;
    setDragFile(value.dragFile);
    position = value.positionSeconds;
    loading = value.busy;
    setMouseCursor(waveform ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    repaint();
  }
  void setTempo(int value) {
    value = juce::jlimit(20, 300, value);
    if (bpm != value) {
      bpm = value;
      repaint();
    }
  }
  void setLanguage(duomi::Language value) {
    language = value;
    setName(tr("音频波形进度条"));
    repaint();
  }
  void paint(juce::Graphics& g) override {
    g.fillAll(juce::Colour(0xff110c1b));
    if (!waveform || waveform->peaks.empty()) {
      g.setColour(juce::Colour(0xffc8b4df));
      g.drawFittedText(
          tr(loading ? "正在加载音频波形…"
                                         : "加载成品音频或打开音频后显示波形"),
          getLocalBounds().reduced(12), juce::Justification::centred, 2);
      return;
    }
    const auto area = waveArea();
    const double duration = waveform->durationSeconds;
    const float fraction =
        duration > 0
            ? static_cast<float>(juce::jlimit(0.0, 1.0, position / duration))
            : 0.0f;
    const float cursor = area.getX() + fraction * area.getWidth();
    g.setFont(12.0f);
    g.setColour(juce::Colour(0xffc8b4df));
    g.drawText(tr("成品音频 · 小节/拍 4/4 · ") +
                   juce::String(bpm) +
                   tr(" BPM · 单击定位 / 按住拖出"),
               12, 8, getWidth() - 24, 18, juce::Justification::centredLeft);
    for (const auto& tick :
         MusicalGrid::ticks(0, duration, bpm, area.getWidth())) {
      const float x =
          area.getX() + (float)(tick.seconds / duration) * area.getWidth();
      g.setColour(juce::Colour(0xff342146));
      g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
      if (tick.label.isNotEmpty() && x < area.getRight() - 25) {
        g.setColour(juce::Colour(0xffc8b4df));
        g.drawText(tick.label,
                   juce::Rectangle<float>(x + 2, area.getBottom() + 12, 66, 18),
                   juce::Justification::left);
      }
    }
    g.setColour(juce::Colour(0xff302041));
    g.fillRect(area.withWidth(fraction * area.getWidth()));
    auto drawWave = [&](juce::Colour colour) {
      g.setColour(colour);
      const int width = juce::jmax(1, static_cast<int>(area.getWidth()));
      const int bins = static_cast<int>(waveform->peaks.size());
      for (int x = 0; x < width; ++x) {
        float low = 0, high = 0;
        const int first = x * bins / width,
                  end = juce::jmin(
                      bins, juce::jmax(first + 1, (x + 1) * bins / width));
        for (int i = first; i < end; ++i) {
          low = juce::jmin(low, waveform->peaks[i].low);
          high = juce::jmax(high, waveform->peaks[i].high);
        }
        const float top = area.getCentreY() - high * area.getHeight() * 0.46f;
        const float bottom = area.getCentreY() - low * area.getHeight() * 0.46f;
        g.fillRect(area.getX() + x, top, 1.0f, juce::jmax(1.0f, bottom - top));
      }
    };
    drawWave(juce::Colour(0xff775994));
    {
      juce::Graphics::ScopedSaveState saved(g);
      g.reduceClipRegion(
          area.withWidth(fraction * area.getWidth()).toNearestInt());
      drawWave(juce::Colour(0xffc58bff));
    }
    g.setColour(juce::Colour(0xff392449));
    g.fillRoundedRectangle(area.getX(), area.getBottom() + 5, area.getWidth(),
                           4, 2);
    g.setColour(juce::Colour(0xffc58bff));
    g.fillRoundedRectangle(area.getX(), area.getBottom() + 5,
                           fraction * area.getWidth(), 4, 2);
    g.setColour(juce::Colours::white);
    g.drawLine(cursor, area.getY(), cursor, area.getBottom() + 9, 1.5f);
    g.fillEllipse(cursor - 3, area.getBottom() + 4, 6, 6);
  }
  void mouseDown(const juce::MouseEvent& event) override {
    FileDragComponent::mouseDown(event);
    clickedWaveform = waveform;
    dragging = event.mods.isLeftButtonDown() && waveform &&
               waveArea().expanded(0, 10).contains(event.position);
  }
  void mouseDrag(const juce::MouseEvent& event) override {
    FileDragComponent::mouseDrag(event);
  }
  void mouseUp(const juce::MouseEvent& event) override {
    if (dragging && !dragAttempted() && event.getDistanceFromDragStart() < 6 &&
        clickedWaveform == waveform)
      moveTo(event.position.x);
    dragging = false;
    clickedWaveform.reset();
  }

 private:
  duomi::Language language = duomi::Language::simplifiedChinese;
  juce::String tr(const char* value) const { return duomi::translate(value, language); }
  int bpm = 120;
  std::shared_ptr<const duomi::AudioPreviewPlayer::Waveform> waveform;
  std::shared_ptr<const duomi::AudioPreviewPlayer::Waveform> clickedWaveform;
  double position = 0;
  bool loading = false, dragging = false;
  juce::Rectangle<float> waveArea() const {
    return getLocalBounds()
        .toFloat()
        .reduced(12)
        .withTrimmedTop(22)
        .withTrimmedBottom(24);
  }
  void moveTo(float x) {
    if (!waveform || !onSeek) return;
    const auto area = waveArea();
    const auto fraction =
        juce::jlimit(0.0, 1.0,
                     static_cast<double>((x - area.getX()) /
                                         juce::jmax(1.0f, area.getWidth())));
    position = fraction * waveform->durationSeconds;
    onSeek(fraction);
    repaint();
  }
};