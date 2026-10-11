/*
  ==============================================================================

    Duomi Suno AI Bridge — 插件编辑器 UI

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "AudioWaveformComponent.h"
#include "CyberTheme.h"
#include "GenerationPrompt.h"
#include "PianoRollComponent.h"
#include "PluginProcessor.h"

//==============================================================================
class DuomiSunoAIBridgeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                              private juce::ChangeListener,
                                              private juce::Timer {
 public:
  DuomiSunoAIBridgeAudioProcessorEditor(DuomiSunoAIBridgeAudioProcessor&);
  ~DuomiSunoAIBridgeAudioProcessorEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  DuomiSunoAIBridgeAudioProcessor& audioProcessor;

  cyber::LookAndFeel theme;
  juce::TooltipWindow tooltips{this, 600};
  juce::Label titleLabel;
  juce::Label bpmLabel;
  cyber::BpmControl bpmControl;
  juce::Label keyLabel;
  juce::TextEditor keyEditor;
  juce::TextButton saveKeyButton{juce::String::fromUTF8("保存 Key")};
  juce::TextButton forgetKeyButton{juce::String::fromUTF8("忘记 Key")};
  juce::Label keyStatusLabel;
  bool keyEdited = false, keyLoadHandled = false, forgettingKey = false;
  juce::Label promptLabel;
  juce::TextEditor promptEditor;
  juce::TextButton generateMidiButton;
  juce::TextButton generateAudioButton;
  juce::Label midiPreviewLabel;
  juce::ComboBox previewMode, trackChoice;
  juce::String trackSignature;
  void refreshTrackChoice();
  juce::Label chordLabel, chordStatusLabel;
  juce::ComboBox chordStep;
  juce::TextButton chordButton{juce::String::fromUTF8("重建钢琴柱式和弦")};
  int inputRevision = 0;
  juce::TextButton openAudioButton{juce::String::fromUTF8("打开音频")};
  juce::TextButton cancelAudioButton{juce::String::fromUTF8("取消下载")};
  juce::Label audioStatusLabel;
  juce::Label progressLabel;
  juce::TextButton importButton{juce::String::fromUTF8("打开 MIDI")};
  PianoRollComponent pianoRoll;
  AudioWaveformComponent audioWaveform;
  std::unique_ptr<juce::FileChooser> fileChooser;
  void refreshPlayback();
  int refreshTicks = 0;
  cyber::TransportButton playButton{cyber::TransportButton::Icon::play};
  cyber::TransportButton pauseButton{cyber::TransportButton::Icon::pause};
  cyber::TransportButton stopButton{cyber::TransportButton::Icon::stop};
  juce::Label dragHintLabel;
  juce::TextButton exportButton{juce::String::fromUTF8("导出 MIDI")};
  juce::Label fileStatusLabel;
  juce::String lastExportMessage;
  bool chooserActive = false;
  void setFileStatus(const juce::String&, bool failed);
  void exportMidi();
  juce::Label statusLabel;
  juce::Label taskLabel;
  juce::TextButton cancelButton{juce::String::fromUTF8("取消等待")};
  juce::TextButton resumeButton{juce::String::fromUTF8("继续查询")};
  juce::TextButton showFileButton{juce::String::fromUTF8("打开 MIDI 目录")};
  juce::TextButton logButton{juce::String::fromUTF8("打开诊断日志")};

  void setStatus(const juce::String& msg);
  void refreshState();
  void changeListenerCallback(juce::ChangeBroadcaster*) override;
  void timerCallback() override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      DuomiSunoAIBridgeAudioProcessorEditor)
};
