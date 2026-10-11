/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include "PluginEditor.h"

//==============================================================================
DuomiSunoAIBridgeAudioProcessor::DuomiSunoAIBridgeAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      )
#endif
{
  generationJob.addChangeListener(this);
  startTimer(100);
}

DuomiSunoAIBridgeAudioProcessor::~DuomiSunoAIBridgeAudioProcessor() {
  stopTimer();
  generationJob.removeChangeListener(this);
  midiTrackJob.cancel();
  audioStemJob.cancel();
  stemPreviewPlayer.cancel();
  generationJob.cancel();
  audioPreviewPlayer.cancel();
  chordJob.cancel();
}
void DuomiSunoAIBridgeAudioProcessor::setPreviewMidi(
    std::shared_ptr<const duomi::GeneratedMidi> midi) {
  midiTrack.store(0);
  midiTrackJob.cancel();
  std::atomic_store(&previewMidi, std::move(midi));
  markSessionChanged();
  loadSelectedMidi();
}
void DuomiSunoAIBridgeAudioProcessor::changeListenerCallback(
    juce::ChangeBroadcaster*) {
  if (isRestoringSession()) return;
  markSessionChanged();
  auto midi = generationJob.getSnapshot().midi;
  if (midi && midi != getOriginalMidi()) setPreviewMidi(std::move(midi));
}

//==============================================================================
const juce::String DuomiSunoAIBridgeAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool DuomiSunoAIBridgeAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool DuomiSunoAIBridgeAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool DuomiSunoAIBridgeAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double DuomiSunoAIBridgeAudioProcessor::getTailLengthSeconds() const {
  return 0.12;
}

int DuomiSunoAIBridgeAudioProcessor::getNumPrograms() {
  return 1;  // NB: some hosts don't cope very well if you tell them there are 0
             // programs, so this should be at least 1, even if you're not
             // really implementing programs.
}

int DuomiSunoAIBridgeAudioProcessor::getCurrentProgram() { return 0; }

void DuomiSunoAIBridgeAudioProcessor::setCurrentProgram(int index) {}

const juce::String DuomiSunoAIBridgeAudioProcessor::getProgramName(int index) {
  return {};
}

void DuomiSunoAIBridgeAudioProcessor::changeProgramName(
    int index, const juce::String& newName) {}

//==============================================================================
void DuomiSunoAIBridgeAudioProcessor::prepareToPlay(double sampleRate,
                                                    int samplesPerBlock) {
  juce::ignoreUnused(samplesPerBlock);
  previewPlayer.prepare(sampleRate);
  audioPreviewPlayer.prepare(sampleRate);
  stemPreviewPlayer.prepare(sampleRate);
}

void DuomiSunoAIBridgeAudioProcessor::releaseResources() {
  previewPlayer.release();
  audioPreviewPlayer.release();
  stemPreviewPlayer.release();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DuomiSunoAIBridgeAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
#if JucePlugin_IsMidiEffect
  juce::ignoreUnused(layouts);
  return true;
#else
  // This is the place where you check if the layout is supported.
  // In this template code we only support mono or stereo.
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

  // This checks if the input layout matches the output layout
#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}
#endif

void DuomiSunoAIBridgeAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  // Expose the MIDI event input required by instrument hosts. Preview playback
  // uses its own generated sequence; host MIDI is currently ignored.
  midiMessages.clear();
  if (isAudioPreviewSelected())
    getAudioPreviewPlayer().render(buffer);
  else
    previewPlayer.render(buffer);
}

//==============================================================================
bool DuomiSunoAIBridgeAudioProcessor::hasEditor() const {
  return true;  // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* DuomiSunoAIBridgeAudioProcessor::createEditor() {
  return new DuomiSunoAIBridgeAudioProcessorEditor(*this);
}

//==============================================================================
void DuomiSunoAIBridgeAudioProcessor::getStateInformation(
    juce::MemoryBlock& destData) {
  duomi::SessionState saved;
  {
    std::lock_guard<std::mutex> lock(sessionMutex);
    if (retainedRestore) {
      destData = retainedRestore->encode();
      return;
    }
    saved.prompt = description;
    saved.audioFile = restoredAudioReference;
  }
  saved.bpm = getGenerationBpm();
  saved.chordBeats = getChordBeats();
  saved.mode = getPreviewMode();
  saved.midiTrack = getMidiTrack();
  saved.audioTrack = getAudioTrack();
  saved.audioSourceTask = getAudioSourceTask();
  saved.stemBatch = audioStemJob.getSnapshot().batch;
  const auto job = generationJob.getSnapshot();
  saved.taskId = job.taskId;
  saved.midiTaskId = job.midiTaskId;
  saved.audioUrl = job.audioUrl;
  saved.resumeAvailable = job.resumeAvailable;
  if (const auto midi = getOriginalMidi()) {
    saved.midiBytes = midi->fileBytes;
    saved.midiName = midi->file.getFileName();
    if (saved.audioUrl.isEmpty()) saved.audioUrl = midi->audioUrl;
  }
  const auto audio = audioPreviewPlayer.getSnapshot();
  // Only local files are restored automatically; never download on project
  // open.
  if (audio.dragFile != juce::File{})
    saved.audioFile = audio.dragFile.getFullPathName();
  if (saved.audioFile.isEmpty() && juce::File::isAbsolutePath(audio.source))
    saved.audioFile = audio.source;
  destData = saved.encode();
}

void DuomiSunoAIBridgeAudioProcessor::setStateInformation(const void* data,
                                                          int sizeInBytes) {
  // Hosts can call off the message thread. Decode bounded data and queue it;
  // all cancellation, file IO and player adoption happen later.
  auto saved = std::make_shared<duomi::SessionState>();
  if (!duomi::SessionState::decode(data, sizeInBytes, *saved)) return;
  std::lock_guard<std::mutex> lock(sessionMutex);
  pendingRestore = saved;
  retainedRestore = saved;
  restoring.store(true);
}

juce::String DuomiSunoAIBridgeAudioProcessor::getDescription() const {
  std::lock_guard<std::mutex> lock(sessionMutex);
  return description;
}
void DuomiSunoAIBridgeAudioProcessor::setDescription(juce::String text) {
  {
    std::lock_guard<std::mutex> lock(sessionMutex);
    text = text.substring(0, 16000);
    if (description == text) return;
    description = std::move(text);
  }
  markSessionChanged();
}
std::shared_ptr<const duomi::GeneratedMidi>
DuomiSunoAIBridgeAudioProcessor::getPreviewMidi() const {
  if (getPreviewMode() == 3) return chordJob.getSnapshot().midi;
  const auto original = getOriginalMidi();
  if (getMidiTrack() == 0) return original;
  const auto selected = midiTrackJob.getSnapshot();
  return selected.source == original && selected.index == getMidiTrack()
             ? selected.midi
             : nullptr;
}
void DuomiSunoAIBridgeAudioProcessor::selectMidiTrack(int index) {
  const auto original = getOriginalMidi();
  index = original ? juce::jlimit(0, (int)original->parts.size(), index) : 0;
  if (midiTrack.exchange(index) != index) {
    previewPlayer.stop();
    midiTrackJob.cancel();
    markSessionChanged();
  }
  loadSelectedMidi();
}
bool DuomiSunoAIBridgeAudioProcessor::selectAudioTrack(int index,
                                                       bool allowDownload) {
  const auto batch = audioStemJob.getSnapshot().batch;
  if (index < 0 || index > (int)batch.stems.size() ||
      stemPreviewPlayer.getSnapshot().busy)
    return false;
  stopPreview();
  if (index == 0) {
    audioTrack.store(0);
    markSessionChanged();
    return true;
  }
  const auto& stem = batch.stems[(size_t)index - 1];
  if (allowDownload && stem.url.isEmpty() && stem.file.isEmpty()) return false;
  stemPreviewPlayer.clear();
  audioTrack.store(index);
  bool started = false;
  if (stem.file.isNotEmpty() && juce::File(stem.file).existsAsFile())
    started = stemPreviewPlayer.importFile(juce::File(stem.file));
  else if (allowDownload && stem.url.isNotEmpty())
    started = stemPreviewPlayer.download(stem.url);
  else if (stem.file.isNotEmpty())
    started = stemPreviewPlayer.importFile(juce::File(stem.file));
  markSessionChanged();
  return started;
}
bool DuomiSunoAIBridgeAudioProcessor::loadFullAudio(juce::String url,
                                                    juce::String task) {
  if (audioStemJob.getSnapshot().busy || stemPreviewPlayer.getSnapshot().busy ||
      audioPreviewPlayer.getSnapshot().busy)
    return false;
  if (!audioPreviewPlayer.download(url)) return false;
  pendingAudioContext = true;
  pendingAudioSource = url;
  pendingAudioTask = std::move(task);
  audioTrack.store(0);
  selectPreviewMode(2);
  markSessionChanged();
  return true;
}
bool DuomiSunoAIBridgeAudioProcessor::importFullAudio(const juce::File& file) {
  if (audioStemJob.getSnapshot().busy || stemPreviewPlayer.getSnapshot().busy ||
      audioPreviewPlayer.getSnapshot().busy)
    return false;
  if (!audioPreviewPlayer.importFile(file)) return false;
  pendingAudioContext = true;
  pendingAudioSource = file.getFullPathName();
  pendingAudioTask.clear();
  audioTrack.store(0);
  selectPreviewMode(2);
  markSessionChanged();
  return true;
}
void DuomiSunoAIBridgeAudioProcessor::selectPreviewMode(int mode) {
  stopPreview();
  const int next = juce::jlimit(1, 3, mode);
  if (previewMode.exchange(next) != next) markSessionChanged();
  loadSelectedMidi();
}
void DuomiSunoAIBridgeAudioProcessor::loadSelectedMidi() {
  const auto midi = getPreviewMidi();
  if (midi != loadedPreviewMidi) {
    loadedPreviewMidi = midi;
    previewPlayer.load(midi);
  }
}
void DuomiSunoAIBridgeAudioProcessor::rebuildChords() {
  chordSource.reset();
  chordJob.cancel();
}
void DuomiSunoAIBridgeAudioProcessor::refreshBackgroundState() {
  previewPlayer.collectRetired();
  audioPreviewPlayer.collectRetired();
  stemPreviewPlayer.collectRetired();
  const auto stemSnapshot = stemPreviewPlayer.getSnapshot();
  if (getAudioTrack() > 0 && !stemSnapshot.busy && stemSnapshot.ready)
    audioStemJob.cache(getAudioTrack(), stemSnapshot.dragFile);
  const auto batch = audioStemJob.getSnapshot().batch;
  juce::String signature =
      batch.sourceTask + juce::String((int)batch.submissionUncertain);
  for (const auto& stem : batch.stems)
    signature += stem.taskId + stem.url + stem.file;
  if (signature != lastStemSignature) {
    lastStemSignature = signature;
    markSessionChanged();
  }
  const auto fullAudio = audioPreviewPlayer.getSnapshot();
  if (pendingAudioContext && !fullAudio.busy) {
    if (!fullAudio.failed && fullAudio.ready &&
        fullAudio.source == pendingAudioSource) {
      if (getAudioSourceTask() != pendingAudioTask) audioStemJob.restore({});
      {
        std::lock_guard<std::mutex> lock(sessionMutex);
        audioSourceTask = pendingAudioTask;
      }
      markSessionChanged();
    }
    pendingAudioContext = false;
  }
  const auto audioFile = fullAudio.dragFile;
  if (audioFile != lastPublishedAudioFile) {
    lastPublishedAudioFile = audioFile;
    markSessionChanged();
  }
  {
    std::lock_guard<std::mutex> lock(sessionMutex);
    if (pendingRestore) {
      activeRestore = std::move(pendingRestore);
      restorePhase = 0;
    }
  }
  if (activeRestore) {
    if (restorePhase == 0) {
      generationJob.cancel();
      midiTrackJob.cancel();
      audioStemJob.cancel();
      stemPreviewPlayer.cancel();
      audioPreviewPlayer.cancel();
      chordJob.cancel();
      stopPreview();
      if (midiTrackJob.getSnapshot().busy || audioStemJob.getSnapshot().busy ||
          stemPreviewPlayer.getSnapshot().busy ||
          generationJob.getSnapshot().busy ||
          audioPreviewPlayer.getSnapshot().busy || chordJob.getSnapshot().busy)
        return;
      const auto& saved = *activeRestore;
      setDescription(saved.prompt);
      {
        std::lock_guard<std::mutex> lock(sessionMutex);
        restoredAudioReference = saved.audioFile;
      }
      setGenerationBpm(saved.bpm);
      setChordBeats(saved.chordBeats);
      chordJob.clear();
      chordSource.reset();
      setPreviewMidi({});
      pendingAudioContext = false;
      audioTrack.store(0);
      stemPreviewPlayer.clear();
      audioStemJob.restore(saved.stemBatch);
      {
        std::lock_guard<std::mutex> lock(sessionMutex);
        audioSourceTask = saved.audioSourceTask;
      }
      audioPreviewPlayer.clear();
      selectPreviewMode(saved.mode);
      ++inputRevision;
      duomi::GenerationJob::Snapshot metadata;
      metadata.taskId = saved.taskId;
      metadata.midiTaskId = saved.midiTaskId;
      metadata.audioUrl = saved.audioUrl;
      metadata.resumeAvailable = saved.resumeAvailable;
      generationJob.restore(std::move(metadata), saved.midiBytes,
                            saved.midiName);
      if (saved.audioFile.isNotEmpty())
        audioPreviewPlayer.importFile(juce::File(saved.audioFile));
      restorePhase = 1;
      return;
    }
    if (generationJob.getSnapshot().busy ||
        audioPreviewPlayer.getSnapshot().busy)
      return;
    if (restorePhase == 1) {
      setPreviewMidi(generationJob.getSnapshot().midi);
      selectMidiTrack(activeRestore->midiTrack);
      selectAudioTrack(activeRestore->audioTrack, false);
      restorePhase = 2;
    }
  } else if (!isRestoringSession()) {
    auto midi = generationJob.getSnapshot().midi;
    if (midi && midi != getOriginalMidi()) setPreviewMidi(midi);
  } else
    return;
  const auto original = getOriginalMidi();
  const auto trackState = midiTrackJob.getSnapshot();
  if (getMidiTrack() > 0 && original && !trackState.busy &&
      (trackState.source != original || trackState.index != getMidiTrack() ||
       (!trackState.midi && !trackState.failed)))
    midiTrackJob.start(original, getMidiTrack());
  if (original && (original != chordSource || chordBpm != getGenerationBpm() ||
                   chordStep != getChordBeats())) {
    chordJob.cancel();
    if (chordJob.getSnapshot().busy) return;
    chordJob.start(original, getGenerationBpm(), getChordBeats());
    chordSource = original;
    chordBpm = getGenerationBpm();
    chordStep = getChordBeats();
  }
  loadSelectedMidi();
  if (activeRestore && !chordJob.getSnapshot().busy &&
      !midiTrackJob.getSnapshot().busy &&
      !stemPreviewPlayer.getSnapshot().busy) {
    stopPreview();
    std::lock_guard<std::mutex> lock(sessionMutex);
    if (retainedRestore == activeRestore) {
      retainedRestore.reset();
      restoring.store(false);
    }
    activeRestore.reset();
  }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new DuomiSunoAIBridgeAudioProcessor();
}
