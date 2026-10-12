#pragma once
#include "DuomiClient.h"
namespace duomi {
struct SessionState {
  int bpm = 120, chordBeats = 2, mode = 1;
  int language = 1;
  int midiTrack = 0, audioTrack = 0;
  juce::String audioSourceTask;
  StemBatch stemBatch;
  bool resumeAvailable = false;
  juce::String prompt, taskId, midiTaskId, audioUrl, midiName, audioFile;
  juce::MemoryBlock midiBytes;
  juce::MemoryBlock encode() const {
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("type", "DuomiSunoAIBridge");
    obj->setProperty("version", 1);
    obj->setProperty("bpm", bpm);
    obj->setProperty("chordBeats", chordBeats);
    obj->setProperty("mode", mode);
    obj->setProperty("language", language);
    obj->setProperty("prompt", prompt);
    obj->setProperty("taskId", taskId);
    obj->setProperty("midiTaskId", midiTaskId);
    obj->setProperty("audioUrl", audioUrl);
    obj->setProperty("resumeAvailable", resumeAvailable);
    obj->setProperty("midiName", midiName);
    obj->setProperty("midi", juce::Base64::toBase64(midiBytes.getData(),
                                                    midiBytes.getSize()));
    obj->setProperty("audioFile", audioFile);
    obj->setProperty("midiTrack", midiTrack);
    obj->setProperty("audioTrack", audioTrack);
    obj->setProperty("audioSourceTask", audioSourceTask);
    juce::DynamicObject::Ptr stems = new juce::DynamicObject;
    stems->setProperty("source", stemBatch.sourceTask);
    stems->setProperty("clip", stemBatch.clipId);
    stems->setProperty("uncertain", stemBatch.submissionUncertain);
    juce::Array<juce::var> list;
    for (const auto& stem : stemBatch.stems) {
      juce::DynamicObject::Ptr item = new juce::DynamicObject;
      item->setProperty("task", stem.taskId);
      item->setProperty("name", stem.name);
      item->setProperty("url", stem.url);
      item->setProperty("file", stem.file);
      list.add(juce::var(item.get()));
    }
    stems->setProperty("tracks", list);
    obj->setProperty("stems", juce::var(stems.get()));
    const auto text = juce::JSON::toString(juce::var(obj.release()), true);
    return {text.toRawUTF8(), text.getNumBytesAsUTF8()};
  }
  static bool decode(const void* data, int size, SessionState& out) {
    if (!data || size <= 0 || size > 24 * 1024 * 1024 ||
        !juce::CharPointer_UTF8::isValidString((const char*)data, size))
      return false;
    juce::var value;
    if (juce::JSON::parse(juce::String::fromUTF8((const char*)data, size),
                          value)
            .failed() ||
        !value.isObject() || value["type"].toString() != "DuomiSunoAIBridge" ||
        !value["version"].isInt() || (int)value["version"] != 1)
      return false;
    for (const char* key : {"bpm", "chordBeats", "mode"})
      if (!value[key].isInt()) return false;
    for (const char* key : {"prompt", "taskId", "midiTaskId", "audioUrl",
                            "midiName", "audioFile", "midi"})
      if (!value[key].isString()) return false;
    SessionState result;
    if (value.hasProperty("language")) {
      if (!value["language"].isInt()) return false;
      result.language = (int)value["language"];
      if (result.language != 1 && result.language != 2) return false;
    }
    result.bpm = (int)value["bpm"];
    result.chordBeats = (int)value["chordBeats"];
    result.mode = (int)value["mode"];
    if (result.bpm < 20 || result.bpm > 300 || result.mode < 1 ||
        result.mode > 3 ||
        (result.chordBeats != 1 && result.chordBeats != 2 &&
         result.chordBeats != 4))
      return false;
    result.prompt = value["prompt"].toString();
    result.taskId = value["taskId"].toString();
    result.midiTaskId = value["midiTaskId"].toString();
    result.audioUrl = value["audioUrl"].toString();
    result.midiName = value["midiName"].toString();
    result.audioFile = value["audioFile"].toString();
    result.resumeAvailable = (bool)value["resumeAvailable"];
    if (result.prompt.length() > 16000 || result.taskId.length() > 512 ||
        result.midiTaskId.length() > 512 || result.audioUrl.length() > 8192 ||
        result.midiName.length() > 512 || result.audioFile.length() > 32768 ||
        (result.audioFile.isNotEmpty() &&
         !juce::File::isAbsolutePath(result.audioFile)))
      return false;
    const auto base64 = value["midi"].toString();
    if (base64.length() > 22369624) return false;
    juce::MemoryOutputStream stream;
    if (!juce::Base64::convertFromBase64(stream, base64) ||
        stream.getDataSize() > 16 * 1024 * 1024)
      return false;
    result.midiBytes = stream.getMemoryBlock();
    if (result.midiBytes.getSize() != 0 &&
        (result.midiBytes.getSize() < 14 ||
         std::memcmp(result.midiBytes.getData(), "MThd", 4) != 0))
      return false;
    for (const auto* field : {"midiTrack", "audioTrack"})
      if (value.hasProperty(field) && !value[field].isInt()) return false;
    result.midiTrack = (int)value["midiTrack"];
    result.audioTrack = (int)value["audioTrack"];
    result.audioSourceTask = value["audioSourceTask"].toString();
    if (result.midiTrack < 0 || result.midiTrack > 65535 ||
        result.audioTrack < 0 || result.audioTrack > 12 ||
        result.audioSourceTask.length() > 512)
      return false;
    if (value.hasProperty("stems")) {
      auto stems = value["stems"];
      if (!stems.isObject() || !stems["tracks"].isArray() ||
          stems["tracks"].size() > 12)
        return false;
      result.stemBatch.sourceTask = stems["source"].toString();
      result.stemBatch.clipId = stems["clip"].toString();
      result.stemBatch.submissionUncertain = (bool)stems["uncertain"];
      if (result.stemBatch.sourceTask.length() > 512 ||
          result.stemBatch.clipId.length() > 512)
        return false;
      for (const auto& item : *stems["tracks"].getArray()) {
        AudioStem stem{item["task"].toString(), item["name"].toString(),
                       item["url"].toString(), item["file"].toString()};
        if (stem.taskId.isEmpty() || stem.taskId.length() > 512 ||
            stem.name.length() > 160 || stem.url.length() > 8192 ||
            stem.file.length() > 32768 ||
            (stem.url.isNotEmpty() &&
             !stem.url.startsWithIgnoreCase("https://")) ||
            (stem.file.isNotEmpty() && !juce::File::isAbsolutePath(stem.file)))
          return false;
        result.stemBatch.stems.push_back(std::move(stem));
      }
      if (result.audioTrack > (int)result.stemBatch.stems.size()) return false;
      if (result.stemBatch.sourceTask.isNotEmpty() &&
          result.stemBatch.sourceTask != result.audioSourceTask)
        return false;
    }
    out = std::move(result);
    return true;
  }
};
}  // namespace duomi