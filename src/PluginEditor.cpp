#include "PluginEditor.h"

DuomiSunoAIBridgeAudioProcessorEditor::DuomiSunoAIBridgeAudioProcessorEditor(
    DuomiSunoAIBridgeAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setLookAndFeel(&theme);
  titleLabel.setText("Duomi Suno AI Bridge", juce::dontSendNotification);
  titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
  addAndMakeVisible(titleLabel);

  keyLabel.setText(juce::String::fromUTF8("Duomi API Key"),
                   juce::dontSendNotification);
  addAndMakeVisible(keyLabel);
  keyEditor.setPasswordCharacter('*');
  keyEditor.setMultiLine(false);
  keyEditor.setTextToShowWhenEmpty(juce::String::fromUTF8("输入 API Key"),
                                   cyber::muted);
  addAndMakeVisible(keyEditor);
  keyEditor.onTextChange = [this] { keyEdited = true; };
  addAndMakeVisible(saveKeyButton);
  addAndMakeVisible(forgetKeyButton);
  addAndMakeVisible(keyStatusLabel);
  keyStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  keyStatusLabel.setJustificationType(juce::Justification::centredRight);
  saveKeyButton.setTooltip(juce::String::fromUTF8(
      "使用当前 Windows 账户加密保存，下次打开自动读取。"));
  forgetKeyButton.setTooltip(
      juce::String::fromUTF8("移除本机保存的 Key；不影响账户或已提交的任务。"));
  saveKeyButton.onClick = [this] {
    audioProcessor.getApiKeyStore().save(keyEditor.getText());
    refreshState();
  };
  forgetKeyButton.onClick = [this] {
    forgettingKey = audioProcessor.getApiKeyStore().forget();
    refreshState();
  };

  promptLabel.setText(juce::String::fromUTF8("音乐描述 (prompt)"),
                      juce::dontSendNotification);
  addAndMakeVisible(promptLabel);
  promptEditor.setMultiLine(false);
  promptEditor.setTextToShowWhenEmpty(
      juce::String::fromUTF8("例如：轻快的钢琴，C 大调"), cyber::muted);
  promptEditor.setText(audioProcessor.getDescription(), false);
  promptEditor.setInputRestrictions(16000);
  promptEditor.onTextChange = [this] {
    audioProcessor.setDescription(promptEditor.getText());
  };
  inputRevision = audioProcessor.getInputRevision();
  addAndMakeVisible(promptEditor);
  bpmLabel.setText(juce::String::fromUTF8("BPM · 滚轮/双击"),
                   juce::dontSendNotification);
  bpmLabel.setColour(juce::Label::textColourId, cyber::purple);
  bpmLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(bpmLabel);
  addAndMakeVisible(bpmControl);
  bpmControl.setBpm(audioProcessor.getGenerationBpm());
  bpmControl.onBpmChange = [this](int bpm) {
    audioProcessor.setGenerationBpm(bpm);
  };
  generateMidiButton.setColour(juce::TextButton::buttonColourId,
                               juce::Colour(0xff542785));

  generateMidiButton.setButtonText(juce::String::fromUTF8("生成 MIDI"));
  addAndMakeVisible(generateMidiButton);
  generateMidiButton.onClick = [this] {
    bpmControl.hideEditor(false);
    audioProcessor.getGenerationJob().start(
        keyEditor.getText(),
        duomi::generationPrompt(promptEditor.getText(), bpmControl.getBpm()));
    refreshState();
  };
  addAndMakeVisible(cancelButton);
  cancelButton.onClick = [this] { audioProcessor.getGenerationJob().cancel(); };
  addAndMakeVisible(resumeButton);
  resumeButton.onClick = [this] {
    audioProcessor.getGenerationJob().start(keyEditor.getText(), {}, true);
    refreshState();
  };

  generateAudioButton.setButtonText(juce::String::fromUTF8("加载成品音频"));
  generateAudioButton.onClick = [this] {
    auto url = audioProcessor.getGenerationJob().getSnapshot().audioUrl;
    if (url.isEmpty())
      if (auto midi = audioProcessor.getPreviewMidi()) url = midi->audioUrl;
    if (audioProcessor.loadFullAudio(
            url, audioProcessor.getGenerationJob().getSnapshot().taskId)) {
      audioProcessor.selectAudioPreview(true);
      previewMode.setSelectedId(2, juce::dontSendNotification);
    }
    refreshState();
  };
  previewMode.addItem(juce::String::fromUTF8("MIDI 试听"), 1);
  previewMode.addItem(juce::String::fromUTF8("成品音频"), 2);
  previewMode.addItem(juce::String::fromUTF8("钢琴柱式和弦"), 3);
  chordLabel.setText(juce::String::fromUTF8("柱式和弦间隔"),
                     juce::dontSendNotification);
  addAndMakeVisible(chordLabel);
  chordStep.addItem(juce::String::fromUTF8("每 1 拍"), 1);
  chordStep.addItem(juce::String::fromUTF8("每 2 拍"), 2);
  chordStep.addItem(juce::String::fromUTF8("每 4 拍"), 4);
  chordStep.setSelectedId(audioProcessor.getChordBeats(),
                          juce::dontSendNotification);
  chordStep.onChange = [this] {
    audioProcessor.setChordBeats(chordStep.getSelectedId());
  };
  chordStep.setTooltip(juce::String::fromUTF8(
      "以当前 BPM 划分间隔；如和弦变化太快或太慢，可修改后重新分析。"));
  addAndMakeVisible(chordStep);
  addAndMakeVisible(chordButton);
  chordButton.onClick = [this] {
    audioProcessor.rebuildChords();
    audioProcessor.refreshBackgroundState();
  };
  addAndMakeVisible(chordStatusLabel);
  chordStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  previewMode.setSelectedId(audioProcessor.getPreviewMode(),
                            juce::dontSendNotification);
  previewMode.onChange = [this] {
    audioProcessor.selectPreviewMode(previewMode.getSelectedId());
    refreshPlayback();
  };
  addAndMakeVisible(previewMode);
  trackChoice.setName(juce::String::fromUTF8("轨道选择"));
  trackChoice.setEditableText(false);
  trackChoice.setTooltip(juce::String::fromUTF8(
      "MIDI 选择已有轨道；音频选择乐器分轨。首次云端分离最多 12 "
      "轨，按基础生成价格 5 倍计费；已完成结果可重复切换。"));
  trackChoice.onChange = [this] {
    const int id = trackChoice.getSelectedId();
    if (audioProcessor.getPreviewMode() == 1)
      audioProcessor.selectMidiTrack(id - 1);
    else if (audioProcessor.getPreviewMode() == 2) {
      if (id == 1000000)
        audioProcessor.getAudioStemJob().start(
            keyEditor.getText(), audioProcessor.getAudioSourceTask());
      else
        audioProcessor.selectAudioTrack(id - 1);
    }
    refreshState();
  };
  addAndMakeVisible(trackChoice);
  addAndMakeVisible(openAudioButton);
  addAndMakeVisible(cancelAudioButton);
  addAndMakeVisible(audioStatusLabel);
  audioStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  cancelAudioButton.onClick = [this] {
    audioProcessor.getAudioPreviewPlayer().cancel();
    audioProcessor.getAudioStemJob().cancel();
  };
  openAudioButton.onClick = [this] {
    if (chooserActive) return;
    chooserActive = true;
    refreshState();
    fileChooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("选择试听音频"), juce::File{},
        "*.mp3;*.wav;*.flac;*.aiff;*.ogg");
    const juce::Component::SafePointer<DuomiSunoAIBridgeAudioProcessorEditor>
        safe(this);
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) {
          if (!safe) return;
          safe->chooserActive = false;
          if (chooser.getResult() != juce::File{} &&
              safe->audioProcessor.importFullAudio(chooser.getResult())) {
            safe->audioProcessor.selectAudioPreview(true);
            safe->previewMode.setSelectedId(2, juce::dontSendNotification);
          }
          safe->refreshState();
        });
  };
  addAndMakeVisible(generateAudioButton);
  midiPreviewLabel.setText(juce::String::fromUTF8("MIDI 试听"),
                           juce::dontSendNotification);
  addAndMakeVisible(midiPreviewLabel);
  playButton.setButtonText(juce::String::fromUTF8("播放"));
  pauseButton.setButtonText(juce::String::fromUTF8("暂停"));
  stopButton.setButtonText(juce::String::fromUTF8("停止"));
  for (auto* button : {&playButton, &pauseButton, &stopButton}) {
    button->setTooltip(button->getButtonText());
    button->setEnabled(false);
    addAndMakeVisible(button);
  }

  addAndMakeVisible(dragHintLabel);
  dragHintLabel.setText(juce::String::fromUTF8("按住下方图形拖入 DAW"),
                        juce::dontSendNotification);
  dragHintLabel.setJustificationType(juce::Justification::centredRight);
  auto dragStatus = [this](const juce::String& message, bool failed) {
    setFileStatus(message, failed);
  };
  pianoRoll.onStatus = dragStatus;
  audioWaveform.onStatus = dragStatus;
  addAndMakeVisible(exportButton);
  exportButton.onClick = [this] { exportMidi(); };
  addAndMakeVisible(fileStatusLabel);
  fileStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  setFileStatus(juce::String::fromUTF8(
                    "钢琴窗拖出 MIDI，波形拖出 WAV；也可打开文件目录后拖入。"),
                false);
  addAndMakeVisible(progressLabel);
  progressLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(pianoRoll);
  addAndMakeVisible(audioWaveform);
  audioWaveform.onSeek = [this](double fraction) {
    audioProcessor.getAudioPreviewPlayer().seek(fraction);
  };
  addAndMakeVisible(importButton);
  importButton.onClick = [this] {
    if (chooserActive) return;
    chooserActive = true;
    refreshState();
    fileChooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("选择 MIDI 文件"), juce::File{}, "*.mid;*.midi");
    const juce::Component::SafePointer<DuomiSunoAIBridgeAudioProcessorEditor>
        safe(this);
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) {
          if (!safe) return;
          safe->chooserActive = false;
          if (chooser.getResult() != juce::File{}) {
            safe->audioProcessor.getGenerationJob().importMidi(
                chooser.getResult());
          }
          safe->refreshState();
        });
  };
  playButton.onClick = [this] { audioProcessor.playPreview(); };
  pauseButton.onClick = [this] { audioProcessor.pausePreview(); };
  stopButton.onClick = [this] { audioProcessor.stopPreview(); };

  addAndMakeVisible(taskLabel);
  taskLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  statusLabel.setColour(juce::Label::backgroundColourId, cyber::panel);
  statusLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(statusLabel);
  addAndMakeVisible(showFileButton);
  showFileButton.onClick = [this] {
    if (audioProcessor.isAudioPreviewSelected()) {
      auto file = audioProcessor.getAudioPreviewPlayer().getSnapshot().dragFile;
      if (file.existsAsFile()) file.revealToUser();
    } else if (auto midi = audioProcessor.getPreviewMidi())
      midi->file.revealToUser();
  };
  addAndMakeVisible(logButton);
  logButton.onClick = [this] {
    auto log = audioProcessor.getGenerationJob().getSnapshot().logFile;
    if (log.existsAsFile()) log.startAsProcess();
  };
  audioProcessor.getGenerationJob().addChangeListener(this);
  audioProcessor.getMidiExportJob().addChangeListener(this);
  setSize(640, 844);
  refreshState();
  // Ensures the last notification racing with worker exit cannot leave controls
  // disabled.
  startTimerHz(30);
}

DuomiSunoAIBridgeAudioProcessorEditor::
    ~DuomiSunoAIBridgeAudioProcessorEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
  audioProcessor.getMidiExportJob().removeChangeListener(this);
  audioProcessor.getGenerationJob().removeChangeListener(this);
}

void DuomiSunoAIBridgeAudioProcessorEditor::setStatus(const juce::String& msg) {
  statusLabel.setText(msg.substring(0, 500), juce::dontSendNotification);
  statusLabel.setTooltip(msg);
}

void DuomiSunoAIBridgeAudioProcessorEditor::refreshState() {
  const bool restoring = audioProcessor.isRestoringSession();
  refreshTrackChoice();
  if (inputRevision != audioProcessor.getInputRevision()) {
    inputRevision = audioProcessor.getInputRevision();
    promptEditor.setText(audioProcessor.getDescription(), false);
    bpmControl.hideEditor(true);
    bpmControl.setBpm(audioProcessor.getGenerationBpm());
    chordStep.setSelectedId(audioProcessor.getChordBeats(),
                            juce::dontSendNotification);
    previewMode.setSelectedId(audioProcessor.getPreviewMode(),
                              juce::dontSendNotification);
  }
  const auto chords = audioProcessor.getChordJob().getSnapshot();
  const auto chordMessage =
      chords.message.isNotEmpty()
          ? chords.message
          : juce::String::fromUTF8(
                "打开或生成 MIDI 后自动另存钢琴柱式和弦，原始 MIDI 保留。");
  chordStatusLabel.setText(chordMessage, juce::dontSendNotification);
  chordStatusLabel.setTooltip(chordMessage);
  chordStatusLabel.setColour(
      juce::Label::textColourId,
      chords.failed ? juce::Colours::salmon : cyber::muted);
  chordStep.setEnabled(!restoring);
  chordButton.setEnabled(!restoring && !chords.busy &&
                         audioProcessor.getOriginalMidi() != nullptr);
  previewMode.setEnabled(!restoring);
  exportButton.setButtonText(juce::String::fromUTF8(
      audioProcessor.getPreviewMode() == 3 ? "导出和弦 MIDI" : "导出 MIDI"));
  const auto keyState = audioProcessor.getApiKeyStore().getSnapshot();
  if (forgettingKey && !keyState.busy) {
    forgettingKey = false;
    if (!keyState.failed) {
      keyEditor.clear();
      keyEdited = true;
      keyLoadHandled = true;
    }
  }
  if (keyState.loaded && !keyLoadHandled) {
    keyLoadHandled = true;
    if (!keyEdited) keyEditor.setText(keyState.key, false);
  }
  const auto keyMessage =
      keyState.saved && !keyState.busy && !keyState.failed &&
              keyEditor.getText().trim() != keyState.key
          ? juce::String::fromUTF8("输入已修改，点击保存更新")
          : keyState.message;
  keyStatusLabel.setText(keyMessage, juce::dontSendNotification);
  keyStatusLabel.setTooltip(keyMessage);
  keyStatusLabel.setColour(
      juce::Label::textColourId,
      keyState.failed ? juce::Colours::salmon : cyber::muted);
  saveKeyButton.setEnabled(!keyState.busy &&
                           keyEditor.getText().trim().isNotEmpty());
  forgetKeyButton.setEnabled(!keyState.busy &&
                             (keyState.saved || keyState.failed));
  const auto state = audioProcessor.getGenerationJob().getSnapshot();
  setStatus(restoring ? juce::String::fromUTF8("正在恢复工程预览…")
                      : state.status);
  statusLabel.setColour(juce::Label::textColourId, state.failed
                                                       ? juce::Colours::salmon
                                                       : juce::Colours::white);
  taskLabel.setText(state.taskId.isEmpty()
                        ? juce::String::fromUTF8(state.midi ? "本地 MIDI 文件"
                                                            : "尚未提交任务")
                        : juce::String::fromUTF8("任务 ID：") + state.taskId,
                    juce::dontSendNotification);
  taskLabel.setTooltip("Music: " + state.taskId +
                       "\nMIDI: " + state.midiTaskId);
  generateMidiButton.setEnabled(!restoring && !state.busy);
  resumeButton.setEnabled(
      !restoring && !state.busy &&
      (state.taskId.isNotEmpty() || state.midiTaskId.isNotEmpty()) &&
      state.resumeAvailable);
  cancelButton.setEnabled(state.busy);
  keyEditor.setEnabled(!restoring && !state.busy);
  promptEditor.setEnabled(!restoring && !state.busy);
  bpmControl.setEnabled(!restoring && !state.busy);
  importButton.setEnabled(!restoring && !state.busy && !chooserActive);
  const auto audio = audioProcessor.getAudioPreviewPlayer().getSnapshot();
  auto audioUrl = state.audioUrl;
  if (audioUrl.isEmpty())
    if (auto midi = audioProcessor.getPreviewMidi()) audioUrl = midi->audioUrl;
  generateAudioButton.setEnabled(
      !restoring && !audioProcessor.getAudioStemJob().getSnapshot().busy &&
      !audio.busy && audioUrl.isNotEmpty());
  openAudioButton.setEnabled(
      !restoring && !audioProcessor.getAudioStemJob().getSnapshot().busy &&
      !audio.busy && !chooserActive);
  const auto stems = audioProcessor.getAudioStemJob().getSnapshot();
  cancelAudioButton.setEnabled(audio.busy || stems.busy);
  cancelAudioButton.setButtonText(
      juce::String::fromUTF8(stems.busy ? "取消分轨" : "取消下载"));
  const auto audioMessage =
      (stems.busy || stems.failed ||
       (!audio.busy && audioProcessor.getAudioTrack() == 0 &&
        stems.message.isNotEmpty()))
          ? stems.message
          : audio.message;
  audioStatusLabel.setText(audioMessage, juce::dontSendNotification);
  audioStatusLabel.setTooltip(audioMessage);
  audioStatusLabel.setColour(
      juce::Label::textColourId,
      (audio.failed || stems.failed) ? juce::Colours::salmon : cyber::muted);
  const auto exported = audioProcessor.getMidiExportJob().getSnapshot();
  exportButton.setEnabled(!restoring && audioProcessor.getPreviewMidi() &&
                          !exported.busy && !chooserActive);
  if (exported.message.isNotEmpty() && exported.message != lastExportMessage) {
    lastExportMessage = exported.message;
    setFileStatus(exported.message, exported.failed);
  }
  const bool audioMode = audioProcessor.isAudioPreviewSelected();
  showFileButton.setButtonText(
      juce::String::fromUTF8(audioMode ? "打开音频目录" : "打开 MIDI 目录"));
  showFileButton.setEnabled(audioMode
                                ? audio.dragFile != juce::File{}
                                : audioProcessor.getPreviewMidi() != nullptr);
  refreshPlayback();
  logButton.setEnabled(state.logFile != juce::File{});
  repaint();
}

void DuomiSunoAIBridgeAudioProcessorEditor::changeListenerCallback(
    juce::ChangeBroadcaster*) {
  refreshState();
}
void DuomiSunoAIBridgeAudioProcessorEditor::refreshTrackChoice() {
  const int mode = audioProcessor.getPreviewMode();
  const auto original = audioProcessor.getOriginalMidi();
  const auto stems = audioProcessor.getAudioStemJob().getSnapshot();
  juce::StringArray names;
  bool allowSeparate = false;
  int selected = 1;
  if (mode == 3)
    names.add(juce::String::fromUTF8("钢琴柱式和弦（固定）"));
  else if (mode == 1) {
    names.add(juce::String::fromUTF8("全部 MIDI 轨道"));
    if (original)
      for (const auto& part : original->parts) names.add(part.name);
    selected = audioProcessor.getMidiTrack() + 1;
  } else {
    names.add(juce::String::fromUTF8("完整混音"));
    for (const auto& stem : stems.batch.stems) names.add(stem.name);
    selected = audioProcessor.getAudioTrack() + 1;
    allowSeparate = audioProcessor.getAudioSourceTask().isNotEmpty();
  }
  const auto signature = juce::String(mode) + names.joinIntoString("\n") +
                         juce::String((int)allowSeparate) +
                         juce::String((int)stems.busy) +
                         juce::String((int)stems.failed);
  if (signature != trackSignature) {
    trackSignature = signature;
    trackChoice.clear(juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i) trackChoice.addItem(names[i], i + 1);
    if (mode == 2) {
      bool pending = false;
      for (const auto& stem : stems.batch.stems) pending |= stem.url.isEmpty();
      if (stems.batch.stems.empty() || pending) {
        trackChoice.addSeparator();
        trackChoice.addItem(
            juce::String::fromUTF8(pending ? "继续查询分轨（不重提）"
                                           : "提取乐器分轨（云端付费）"),
            1000000);
        trackChoice.setItemEnabled(
            1000000,
            allowSeparate && !stems.busy && !stems.batch.submissionUncertain);
      }
      for (int i = 0; i < (int)stems.batch.stems.size(); ++i)
        trackChoice.setItemEnabled(
            i + 2, stems.batch.stems[(size_t)i].url.isNotEmpty() ||
                       stems.batch.stems[(size_t)i].file.isNotEmpty());
    }
  }
  if (mode == 2 && !allowSeparate)
    trackChoice.setTooltip(
        juce::String::fromUTF8("本地混音没有云端歌曲 "
                               "ID。先点击「加载成品音频」关联当前生成歌曲，再"
                               "提取乐器分轨；无需重新生成歌曲。"));
  else if (mode == 2)
    trackChoice.setTooltip(juce::String::fromUTF8(
        "首次云端分离最多 12 轨，基础生成价格的 5 "
        "倍；已完成结果可重复选择、试听和拖出，不重复提交。"));
  else
    trackChoice.setTooltip(juce::String::fromUTF8(
        mode == 3 ? "柱式和弦是固定的独立钢琴轨。"
                  : "选择 MIDI "
                    "内已有的乐器轨道；试听、钢琴窗、导出与拖拽同步切换。"));
  trackChoice.setSelectedId(selected, juce::dontSendNotification);
  trackChoice.setEnabled(
      mode != 3 && !audioProcessor.isRestoringSession() &&
      (mode != 2 ||
       !audioProcessor.getAudioPreviewPlayer().getSnapshot().busy));
  if (mode == 1 && audioProcessor.getMidiTrack() > 0) {
    const auto track = audioProcessor.getMidiTrackJob().getSnapshot();
    if (track.message.isNotEmpty()) setFileStatus(track.message, track.failed);
  }
}
void DuomiSunoAIBridgeAudioProcessorEditor::refreshPlayback() {
  refreshTrackChoice();
  auto& player = audioProcessor.getPreviewPlayer();
  player.collectRetired();
  audioProcessor.getAudioPreviewPlayer().collectRetired();
  auto snapshot = player.getSnapshot();
  const auto audio = audioProcessor.getAudioPreviewPlayer().getSnapshot();
  const bool audioMode = audioProcessor.isAudioPreviewSelected();
  if (audioMode) {
    snapshot.state = static_cast<duomi::MidiPreviewPlayer::State>(audio.state);
    snapshot.positionSeconds = audio.positionSeconds;
    snapshot.durationSeconds = audio.durationSeconds;
  }
  const auto midi = audioProcessor.getPreviewMidi();
  using State = duomi::MidiPreviewPlayer::State;
  const bool ready = !audioProcessor.isRestoringSession() &&
                     (audioMode ? audio.ready : midi != nullptr);
  playButton.setEnabled(ready && snapshot.state != State::playing);
  pauseButton.setEnabled(ready && snapshot.state == State::playing);
  stopButton.setEnabled(ready && snapshot.state != State::stopped);
  const auto time = [](double seconds) {
    const int value = static_cast<int>(seconds);
    return juce::String(value / 60) + ":" +
           juce::String(value % 60).paddedLeft('0', 2);
  };
  const char* status = snapshot.state == State::playing    ? "播放中"
                       : snapshot.state == State::paused   ? "已暂停"
                       : snapshot.state == State::finished ? "已结束"
                                                           : "已停止";
  progressLabel.setText(
      juce::String::fromUTF8(status) + "  " + time(snapshot.positionSeconds) +
          " / " +
          time(audioMode ? audio.durationSeconds
                         : (midi ? midi->durationSeconds : 0.0)),
      juce::dontSendNotification);
  pianoRoll.setVisible(!audioMode);
  audioWaveform.setVisible(audioMode);
  pianoRoll.setTempo(audioProcessor.getGenerationBpm());
  audioWaveform.setTempo(audioProcessor.getGenerationBpm());
  audioWaveform.setPreview(audio);
  pianoRoll.setMidi(midi);
  pianoRoll.setPosition(player.getSnapshot().positionSeconds);
}
void DuomiSunoAIBridgeAudioProcessorEditor::timerCallback() {
  audioProcessor.refreshBackgroundState();
  refreshPlayback();
  if (++refreshTicks % 8 == 0) refreshState();
}

void DuomiSunoAIBridgeAudioProcessorEditor::paint(juce::Graphics& g) {
  g.fillAll(cyber::background);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff251139), 0, 0,
                                         cyber::background, 450, 300, false));
  g.fillRect(getLocalBounds());
  g.setColour(cyber::purple.withAlpha(0.045f));
  for (int x = 0; x < getWidth(); x += 32)
    g.drawVerticalLine(x, 0, (float)getHeight());
  for (int y = 0; y < getHeight(); y += 32)
    g.drawHorizontalLine(y, 0, (float)getWidth());
  g.setColour(cyber::edge);
  g.drawHorizontalLine(48, 16, (float)getWidth() - 16);
  g.setColour(cyber::pink);
  g.fillRect(16, 48, 68, 2);
  g.setColour(cyber::purple);
  g.drawRect(pianoRoll.getBounds().expanded(1), 1);
}

void DuomiSunoAIBridgeAudioProcessorEditor::resized() {
  const int w = getWidth() - 32;
  titleLabel.setBounds(16, 16, w, 28);
  keyLabel.setBounds(16, 56, 150, 20);
  keyStatusLabel.setBounds(174, 56, w - 158, 20);
  keyEditor.setBounds(16, 80, w - 208, 30);
  saveKeyButton.setBounds(16 + w - 196, 80, 92, 30);
  forgetKeyButton.setBounds(16 + w - 92, 80, 92, 30);
  promptLabel.setBounds(16, 122, w - 120, 20);
  bpmLabel.setBounds(16 + w - 108, 122, 108, 20);
  bpmControl.setBounds(16 + w - 108, 146, 108, 34);
  promptEditor.setBounds(16, 146, w - 120, 34);
  generateMidiButton.setBounds(16, 194, 200, 36);
  cancelButton.setBounds(228, 194, 120, 36);
  resumeButton.setBounds(360, 194, 120, 36);
  generateAudioButton.setBounds(16, 240, 200, 32);
  importButton.setBounds(228, 240, 150, 32);
  midiPreviewLabel.setVisible(false);
  previewMode.setBounds(16, 278, 150, 26);
  trackChoice.setBounds(176, 278, 264, 26);
  progressLabel.setBounds(446, 282, w - 430, 20);
  pianoRoll.setBounds(16, 352, w, 158);
  audioWaveform.setBounds(pianoRoll.getBounds());
  playButton.setBounds(16, 308, 92, 32);
  pauseButton.setBounds(116, 308, 92, 32);
  stopButton.setBounds(216, 308, 92, 32);
  dragHintLabel.setBounds(320, 308, w - 304, 32);
  taskLabel.setBounds(16, 516, w, 24);
  statusLabel.setBounds(16, 544, w, 50);
  showFileButton.setBounds(16, 606, 180, 28);
  logButton.setBounds(208, 606, 180, 28);
  exportButton.setBounds(400, 606, w - 384, 28);
  fileStatusLabel.setBounds(16, 642, w, 32);
  openAudioButton.setBounds(16, 686, 150, 28);
  cancelAudioButton.setBounds(178, 686, 120, 28);
  audioStatusLabel.setBounds(16, 718, w, 30);
  chordLabel.setBounds(16, 758, 150, 28);
  chordStep.setBounds(178, 758, 120, 28);
  chordButton.setBounds(320, 758, w - 304, 28);
  chordStatusLabel.setBounds(16, 794, w, 42);
}

void DuomiSunoAIBridgeAudioProcessorEditor::setFileStatus(
    const juce::String& message, bool failed) {
  fileStatusLabel.setText(message, juce::dontSendNotification);
  fileStatusLabel.setTooltip(message);
  fileStatusLabel.setColour(juce::Label::textColourId,
                            failed ? juce::Colours::salmon : cyber::muted);
}
void DuomiSunoAIBridgeAudioProcessorEditor::exportMidi() {
  const auto midi = audioProcessor.getPreviewMidi();
  if (!midi || chooserActive ||
      audioProcessor.getMidiExportJob().getSnapshot().busy)
    return;
  const auto source =
      midi->file;  // Keep the chosen clip even if a new generation finishes.
  chooserActive = true;
  refreshState();
  fileChooser = std::make_unique<juce::FileChooser>(
      juce::String::fromUTF8("导出 MIDI"),
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
          .getChildFile(source.getFileName())
          .withFileExtension(".mid"),
      "*.mid;*.midi");
  const juce::Component::SafePointer<DuomiSunoAIBridgeAudioProcessorEditor>
      safe(this);
  fileChooser->launchAsync(
      juce::FileBrowserComponent::saveMode |
          juce::FileBrowserComponent::canSelectFiles |
          juce::FileBrowserComponent::warnAboutOverwriting,
      [safe, source](const juce::FileChooser& chooser) {
        if (!safe) return;
        safe->chooserActive = false;
        const auto destination = chooser.getResult();
        if (destination != juce::File{} &&
            !safe->audioProcessor.getMidiExportJob().start(source, destination))
          safe->setFileStatus(
              juce::String::fromUTF8("暂时无法开始导出，请稍后重试。"), true);
        safe->refreshState();
      });
}
