/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "ApiKeyStore.h"
#include "AudioPreviewPlayer.h"
#include "AudioStemJob.h"
#include "ChordExtractionJob.h"
#include "GenerationJob.h"
#include "MidiExportJob.h"
#include "MidiPreviewPlayer.h"
#include "MidiTrackJob.h"
#include "SessionState.h"

//==============================================================================
/**
 */
class DuomiSunoAIBridgeAudioProcessor : public juce::AudioProcessor,
                                        private juce::ChangeListener,
                                        private juce::Timer {
 public:
  //==============================================================================
  DuomiSunoAIBridgeAudioProcessor();
  ~DuomiSunoAIBridgeAudioProcessor() override;

  //==============================================================================
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  //==============================================================================
  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  //==============================================================================
  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  //==============================================================================
  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String& newName) override;

  //==============================================================================
  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  duomi::MidiExportJob& getMidiExportJob() { return midiExportJob; }
  duomi::ApiKeyStore& getApiKeyStore() { return apiKeyStore; }
  duomi::GenerationJob& getGenerationJob() { return generationJob; }
  duomi::MidiPreviewPlayer& getPreviewPlayer() { return previewPlayer; }
  std::shared_ptr<const duomi::GeneratedMidi> getPreviewMidi() const;
  int getMidiTrack() const { return midiTrack.load(); }
  int getAudioTrack() const { return audioTrack.load(); }
  void selectMidiTrack(int);
  bool selectAudioTrack(int, bool allowDownload = true);
  duomi::MidiTrackJob& getMidiTrackJob() { return midiTrackJob; }
  duomi::AudioStemJob& getAudioStemJob() { return audioStemJob; }
  bool loadFullAudio(juce::String url, juce::String task);
  bool importFullAudio(const juce::File&);
  juce::String getAudioSourceTask() const {
    std::lock_guard<std::mutex> lock(sessionMutex);
    return audioSourceTask;
  }
  void setPreviewMidi(std::shared_ptr<const duomi::GeneratedMidi>);
  std::shared_ptr<const duomi::GeneratedMidi> getOriginalMidi() const {
    return std::atomic_load(&previewMidi);
  }
  duomi::ChordExtractionJob& getChordJob() { return chordJob; }
  void rebuildChords();
  int getChordBeats() const { return chordBeats.load(); }
  void setChordBeats(int beats) {
    const int next = beats == 1 || beats == 4 ? beats : 2;
    if (chordBeats.exchange(next) != next) markSessionChanged();
  }
  juce::String getDescription() const;
  void setDescription(juce::String);
  bool isRestoringSession() const { return restoring.load(); }
  int getInputRevision() const { return inputRevision.load(); }
  // Message-thread service used by the timer, editor and offline host harness.
  void refreshBackgroundState();
  duomi::AudioPreviewPlayer& getAudioPreviewPlayer() {
    return audioTrack.load() > 0 ? stemPreviewPlayer : audioPreviewPlayer;
  }
  int getGenerationBpm() const { return generationBpm.load(); }
  void setGenerationBpm(int bpm) {
    const int next = juce::jlimit(20, 300, bpm);
    if (generationBpm.exchange(next) != next) markSessionChanged();
  }
  int getPreviewMode() const { return previewMode.load(); }
  bool isAudioPreviewSelected() const { return getPreviewMode() == 2; }
  void selectPreviewMode(int mode);
  void selectAudioPreview(bool audio) { selectPreviewMode(audio ? 2 : 1); }
  void playPreview() {
    if (isAudioPreviewSelected())
      getAudioPreviewPlayer().play();
    else
      previewPlayer.play();
  }
  void pausePreview() {
    if (isAudioPreviewSelected())
      getAudioPreviewPlayer().pause();
    else
      previewPlayer.pause();
  }
  void stopPreview() {
    previewPlayer.stop();
    audioPreviewPlayer.stop();
    stemPreviewPlayer.stop();
  }

 private:
  duomi::ApiKeyStore apiKeyStore;
  duomi::GenerationJob generationJob;
  duomi::MidiExportJob midiExportJob;
  duomi::MidiPreviewPlayer previewPlayer;
  duomi::AudioPreviewPlayer audioPreviewPlayer, stemPreviewPlayer;
  duomi::MidiTrackJob midiTrackJob;
  duomi::AudioStemJob audioStemJob;
  std::atomic<int> midiTrack{0}, audioTrack{0};
  juce::String audioSourceTask, lastStemSignature{"0"};
  bool pendingAudioContext = false;
  juce::String pendingAudioSource, pendingAudioTask;
  std::atomic<int> generationBpm{120};
  duomi::ChordExtractionJob chordJob;
  std::atomic<int> previewMode{1}, chordBeats{2}, inputRevision{0};
  std::atomic<bool> restoring{false};
  mutable std::mutex sessionMutex;
  juce::String description, restoredAudioReference;
  juce::File lastPublishedAudioFile;
  void markSessionChanged() {
    if (!isRestoringSession())
      updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                            .withNonParameterStateChanged(true));
  }
  std::shared_ptr<const duomi::SessionState> pendingRestore, retainedRestore,
      activeRestore;
  int restorePhase = 0;
  std::shared_ptr<const duomi::GeneratedMidi> loadedPreviewMidi, chordSource;
  int chordBpm = 0, chordStep = 0;
  void loadSelectedMidi();
  void timerCallback() override { refreshBackgroundState(); }
  std::shared_ptr<const duomi::GeneratedMidi> previewMidi;
  void changeListenerCallback(juce::ChangeBroadcaster*) override;
  //==============================================================================
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuomiSunoAIBridgeAudioProcessor)
};
