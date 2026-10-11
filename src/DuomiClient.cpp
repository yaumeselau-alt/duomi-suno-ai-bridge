#include "DuomiClient.h"

#include <array>
#include <cmath>
#include <cstring>

namespace duomi {
namespace {
constexpr size_t maxBodyBytes = 16 * 1024 * 1024;

bool percussionName(const juce::String& name) {
  const auto lower =
      name.toLowerCase().replace("hi-hat", "hihat").replace("hi hat", "hihat");
  // Pitched steel drums are melodic instruments, not a GM drum kit.
  if (lower.contains("steel drum") ||
      name.contains(juce::String::fromUTF8("钢鼓")))
    return false;
  const auto words =
      juce::StringArray::fromTokens(lower, " _-/().0123456789", "");
  for (const auto* word :
       {"drum", "drums", "drumkit", "percussion", "perc", "kick", "snare",
        "hihat", "cymbal", "cymbals", "shaker", "tambourine", "claps"})
    if (words.contains(word)) return true;
  return name.contains(juce::String::fromUTF8("鼓")) ||
         name.contains(juce::String::fromUTF8("打击乐"));
}

bool isMidi(const juce::MemoryBlock& bytes) {
  return bytes.getSize() >= 14 && std::memcmp(bytes.getData(), "MThd", 4) == 0;
}

juce::String bodyText(const HttpResponse& response) {
  if (response.body.getSize() == 0) return {};
  auto* data = static_cast<const char*>(response.body.getData());
  auto size = static_cast<int>(response.body.getSize());
  if (!juce::CharPointer_UTF8::isValidString(data, size))
    return "[non-UTF8 binary response: " + juce::String(size) + " bytes]";
  return juce::String::fromUTF8(data, size);
}

juce::Result readJson(const HttpResponse& response, juce::var& json) {
  if (response.error.isNotEmpty()) return juce::Result::fail(response.error);
  if (response.status < 200 || response.status >= 300) {
    auto message = juce::String("HTTP ") + juce::String(response.status);
    if (response.status == 401 || response.status == 403)
      message += juce::String::fromUTF8("：密钥无效或接口权限不足");
    if (response.status == 429)
      message += juce::String::fromUTF8("：请求过于频繁，请稍后再试");
    juce::var error;
    if (juce::JSON::parse(bodyText(response), error).wasOk())
      message += " " + error["msg"].toString();
    return juce::Result::fail(message);
  }
  if (auto result = juce::JSON::parse(bodyText(response), json);
      result.failed())
    return juce::Result::fail(
        juce::String::fromUTF8("接口未返回有效 JSON，详见诊断日志"));
  if (!json.isObject() && !json.isArray() && !json.isString())
    return juce::Result::fail(
        juce::String::fromUTF8("接口返回空结果或未知数据结构"));
  if (json.hasProperty("code")) {
    const auto code = json["code"].toString();
    if (code != "0" && code != "200")
      return juce::Result::fail("API " + code + juce::String::fromUTF8("：") +
                                json["msg"].toString());
  }
  return juce::Result::ok();
}

juce::var firstData(const juce::var& value) {
  const auto data = value.hasProperty("data") ? value["data"] : value;
  if (auto* array = data.getArray())
    return array->isEmpty() ? juce::var{} : (*array)[0];
  return data;
}

// Only known MIDI payload fields are inspected, never audio_url or arbitrary
// strings such as error messages. Unknown schemas fail with the raw JSON
// logged.
juce::String midiPayload(const juce::var& value, int depth = 0) {
  if (depth > 5) return {};
  if (value.isString()) return value.toString();
  if (auto* array = value.getArray())
    for (const auto& item : *array)
      if (auto found = midiPayload(item, depth + 1); found.isNotEmpty())
        return found;
  for (const auto* name : {"midi_url", "download_url", "file_url", "url",
                           "midi_base64", "base64", "midi", "data"})
    if (value.hasProperty(name))
      if (auto found = midiPayload(value[name], depth + 1); found.isNotEmpty())
        return found;
  return {};
}

bool invalidModel(const HttpResponse& response) {
  juce::var json;
  if (juce::JSON::parse(bodyText(response), json).failed()) return false;
  if (response.status != 400 && json["code"].toString() != "400") return false;
  auto msg = json["msg"].toString().toLowerCase();
  return (msg.contains("model") ||
          msg.contains(juce::String::fromUTF8("模型")) || msg.contains("mv")) &&
         (msg.contains("invalid") || msg.contains("unsupported") ||
          msg.contains(juce::String::fromUTF8("非法")) ||
          msg.contains(juce::String::fromUTF8("不支持")) ||
          msg.contains(juce::String::fromUTF8("无效")));
}

bool isTaskId(const juce::String& text) {
  return text.length() == 36 && text[8] == '-' && text[13] == '-' &&
         text[18] == '-' && text[23] == '-' &&
         text.removeCharacters("-").length() == 32 &&
         text.containsOnly("0123456789abcdefABCDEF-");
}

bool isNumber(const juce::var& value) {
  return value.isInt() || value.isInt64() || value.isDouble();
}
}  // namespace

void Cancellation::cancel() {
  cancelled.store(true);
  wake.signal();
  std::shared_ptr<juce::WebInputStream> current;
  {
    std::lock_guard<std::mutex> lock(mutex);
    current = stream;
  }
  if (current) current->cancel();
}

bool Cancellation::wait(int milliseconds) {
  if (!isCancelled() && milliseconds > 0) wake.wait(milliseconds);
  return !isCancelled();
}

void Cancellation::setStream(std::shared_ptr<juce::WebInputStream> next) {
  std::lock_guard<std::mutex> lock(mutex);
  stream = std::move(next);
  if (stream && isCancelled()) stream->cancel();
}

DuomiClient::DuomiClient(ClientOptions settings, Transport http)
    : options(std::move(settings)),
      transport(http ? std::move(http) : requestNative) {}

juce::String DuomiClient::redact(juce::String text, const juce::String& key) {
  if (key.isEmpty()) return text;
  text = text.replace(key, "[REDACTED]");
  return text.replace(juce::URL::addEscapeChars(key, true), "[REDACTED]");
}

HttpResponse DuomiClient::requestNative(const HttpRequest& request,
                                        Cancellation& cancel) {
  HttpResponse response;
  if (cancel.isCancelled()) {
    response.error = juce::String::fromUTF8("已取消");
    return response;
  }
  auto stream = std::make_shared<juce::WebInputStream>(request.url, false);
  stream->withConnectionTimeout(15000)
      .withCustomRequestCommand(request.method)
      .withExtraHeaders(request.headers)
      .withNumRedirectsToFollow(0);
  // Authenticated endpoints do not auto-follow redirects: never forward a key
  // to an unverified redirect destination. CDN downloads use a fresh request.
  cancel.setStream(stream);
  if (stream->connect(nullptr)) {
    response.status = stream->getStatusCode();
    response.contentType = stream->getResponseHeaders()["Content-Type"];
    response.location = stream->getResponseHeaders()["Location"];
    char buffer[8192];
    while (!cancel.isCancelled() && !stream->isExhausted()) {
      const int count = stream->read(buffer, sizeof(buffer));
      if (count <= 0) break;
      if (response.body.getSize() + static_cast<size_t>(count) > maxBodyBytes) {
        response.error = juce::String::fromUTF8("响应超过 16 MB 限制");
        break;
      }
      response.body.append(buffer, static_cast<size_t>(count));
    }
    if (stream->isError() && response.error.isEmpty())
      response.error = juce::String::fromUTF8("网络读取失败");
  } else {
    response.error = juce::String::fromUTF8(
        "连接失败或超时，请检查网络；生成请求不会自动重发");
  }
  cancel.setStream(nullptr);
  if (cancel.isCancelled()) response.error = juce::String::fromUTF8("已取消");
  return response;
}

HttpResponse DuomiClient::request(const HttpRequest& req,
                                  const juce::String& key,
                                  Cancellation& cancel) {
  if (cancel.isCancelled()) {
    HttpResponse r;
    r.error = juce::String::fromUTF8("已取消");
    return r;
  }
  auto response = transport(req, cancel);
  auto details = juce::Time::getCurrentTime().toISO8601(true) + " " +
                 req.method + " " + redact(req.url.toString(true), key) +
                 " HTTP " + juce::String(response.status) + "\n";
  if (isMidi(response.body))
    details += "MIDI binary: " +
               juce::String(static_cast<juce::int64>(response.body.getSize())) +
               " bytes\n";
  else
    details += redact(bodyText(response), key) + "\n";
  details += redact(response.error, key) + "\n";
  if (options.logFile != juce::File{}) {
    options.logFile.getParentDirectory().createDirectory();
    options.logFile.appendText(details);
  }
  DBG(details);
  return response;
}

juce::Result DuomiClient::decodeMidi(HttpResponse response,
                                     const juce::String& key,
                                     Cancellation& cancel,
                                     juce::MemoryBlock& bytes, int depth) {
  if (cancel.isCancelled())
    return juce::Result::fail(juce::String::fromUTF8("已取消"));
  if (depth > 3)
    return juce::Result::fail(juce::String::fromUTF8("MIDI 下载链接嵌套过多"));
  if (response.error.isNotEmpty()) return juce::Result::fail(response.error);
  if (response.status >= 300 && response.status < 400 &&
      response.location.startsWithIgnoreCase("https://"))
    return decodeMidi(
        request({juce::URL(response.location), "GET", {}}, key, cancel), key,
        cancel, bytes, depth + 1);
  if (response.status >= 200 && response.status < 300 &&
      isMidi(response.body)) {
    bytes = std::move(response.body);
    return juce::Result::ok();
  }
  juce::var json;
  if (auto result = readJson(response, json); result.failed()) return result;
  auto noteData = firstData(json);
  if (noteData.hasProperty("instruments")) return notesToMidi(noteData, bytes);
  auto payload = midiPayload(json);
  if (payload.startsWithIgnoreCase("https://")) {
    // Intentionally omit API credentials when downloading a returned CDN URL.
    return decodeMidi(request({juce::URL(payload), "GET", {}}, key, cancel),
                      key, cancel, bytes, depth + 1);
  }
  if (payload.startsWithIgnoreCase("data:") && payload.contains(";base64,"))
    payload = payload.fromFirstOccurrenceOf(";base64,", false, false);
  juce::MemoryOutputStream decoded;
  if (payload.isNotEmpty() &&
      juce::Base64::convertFromBase64(decoded, payload)) {
    bytes = decoded.getMemoryBlock();
    if (isMidi(bytes)) return juce::Result::ok();
  }
  return juce::Result::fail(juce::String::fromUTF8(
      "MIDI 响应中没有有效的 MIDI 文件、HTTPS 下载链接或 base64；详见日志"));
}

juce::Result DuomiClient::notesToMidi(const juce::var& data,
                                      juce::MemoryBlock& bytes) {
  const auto* instruments = data["instruments"].getArray();
  if (instruments == nullptr || instruments->isEmpty())
    return juce::Result::fail(
        juce::String::fromUTF8("MIDI 结果中没有乐器轨道"));
  // The API timestamps are seconds, not ticks. A reference tempo of 120 BPM
  // with 960 PPQ preserves absolute timing to within half of a 1/1920 s tick.
  // It is an export timebase, not an inferred musical tempo.
  constexpr double ticksPerSecond = 1920.0;
  juce::MidiFile midi;
  midi.setTicksPerQuarterNote(960);
  juce::MidiMessageSequence tempo;
  tempo.addEvent(juce::MidiMessage::tempoMetaEvent(500000));
  midi.addTrack(tempo);
  int nextChannel = 1, count = 0;
  for (const auto& instrument : *instruments) {
    if (static_cast<bool>(instrument["is_drum"]) ||
        percussionName(instrument["name"].toString()))
      continue;
    const auto* notes = instrument["notes"].getArray();
    if (notes == nullptr)
      return juce::Result::fail(
          juce::String::fromUTF8("MIDI 乐器缺少 notes 数组"));
    const auto& program = instrument["program"];
    if (!isNumber(program) || static_cast<double>(program) < 0.0 ||
        static_cast<double>(program) > 127.0 ||
        std::floor(static_cast<double>(program)) !=
            static_cast<double>(program))
      return juce::Result::fail(
          juce::String::fromUTF8("MIDI 乐器 program 无效"));
    // Reserve GM channel 10 for percussion; never emit it here.
    if (nextChannel == 10) ++nextChannel;
    if (nextChannel > 16)
      return juce::Result::fail(
          juce::String::fromUTF8("MIDI 旋律乐器超过 15 个通道"));
    const int channel = nextChannel++;
    juce::MidiMessageSequence track;
    track.addEvent(
        juce::MidiMessage::textMetaEvent(3, instrument["name"].toString()));
    track.addEvent(
        juce::MidiMessage::programChange(channel, static_cast<int>(program)));
    for (const auto& note : *notes) {
      for (const auto* field : {"pitch", "start", "end", "velocity"})
        if (!isNumber(note[field]))
          return juce::Result::fail(
              juce::String::fromUTF8("MIDI 音符缺少有效数值字段"));
      const double pitch = note["pitch"], start = note["start"],
                   end = note["end"], velocity = note["velocity"];
      if (!std::isfinite(pitch) || !std::isfinite(start) ||
          !std::isfinite(end) || !std::isfinite(velocity) || pitch < 0.0 ||
          pitch > 127.0 || std::floor(pitch) != pitch || velocity < 0.0 ||
          velocity > 1.0 || start < 0.0 || end > 86400.0)
        return juce::Result::fail(
            juce::String::fromUTF8("MIDI 音符数值超出有效范围"));
      if (end <= start || velocity == 0.0) continue;
      if (++count > 100000)
        return juce::Result::fail(
            juce::String::fromUTF8("MIDI 音符数量超过限制"));
      auto on = juce::MidiMessage::noteOn(
          channel, static_cast<int>(pitch),
          static_cast<juce::uint8>(
              juce::jlimit(1, 127, juce::roundToInt(velocity * 127.0))));
      auto off = juce::MidiMessage::noteOff(channel, static_cast<int>(pitch));
      on.setTimeStamp(std::round(start * ticksPerSecond));
      off.setTimeStamp(juce::jmax(on.getTimeStamp() + 1.0,
                                  std::round(end * ticksPerSecond)));
      track.addEvent(on);
      track.addEvent(off);
    }
    midi.addTrack(track);
  }
  if (count == 0)
    return juce::Result::fail(
        juce::String::fromUTF8("MIDI 中没有可播放的完整音符"));
  juce::MemoryOutputStream stream;
  if (!midi.writeTo(stream, 1))
    return juce::Result::fail(juce::String::fromUTF8("无法编码标准 MIDI 文件"));
  bytes = stream.getMemoryBlock();
  return juce::Result::ok();
}

juce::Result DuomiClient::waitForTask(
    const juce::String& key, const juce::String& taskId, Cancellation& cancel,
    juce::var& data, const std::function<void(const juce::String&)>& report) {
  const auto auth =
      "Authorization: " + key + "\r\nContent-Type: application/json\r\n";
  for (int poll = 0; poll < options.maxPolls; ++poll) {
    if (cancel.isCancelled())
      return juce::Result::fail(
          juce::String::fromUTF8("已取消等待，云端任务可能仍会继续"));
    HttpResponse response;
    for (const auto& host : options.generateHosts) {
      response = request(
          {juce::URL(host + "/api/suno/feed").withParameter("task_id", taskId),
           "GET", auth},
          key, cancel);
      if (response.status != 0 && response.status != 404) break;
    }
    juce::var json;
    if (auto result = readJson(response, json); result.failed()) return result;
    data = firstData(json);
    const auto state = data["state"].toString().toLowerCase();
    if (state == "succeeded") return juce::Result::ok();
    if (state == "error" || state == "failed")
      return juce::Result::fail(juce::String::fromUTF8("任务失败：") +
                                data["msg"].toString());
    if (state != "pending" && state != "running" && state != "queued" &&
        state != "processing")
      return juce::Result::fail(
          juce::String::fromUTF8("任务查询返回未知状态：") + state);
    report(state == "pending" || state == "queued"
               ? juce::String::fromUTF8("排队中")
               : juce::String::fromUTF8("处理中"));
    if (!cancel.wait(options.pollIntervalMs))
      return juce::Result::fail(
          juce::String::fromUTF8("已取消等待，云端任务可能仍会继续"));
  }
  return juce::Result::fail(juce::String::fromUTF8(
      "等待任务超时，保留任务 ID，可继续查询而无需重新提交"));
}

juce::Result DuomiClient::filterPercussion(juce::MemoryBlock& bytes) {
  if (!isMidi(bytes))
    return juce::Result::fail(
        juce::String::fromUTF8("下载内容不是标准 MIDI 文件"));
  juce::MemoryInputStream input(bytes, false);
  juce::MidiFile original;
  int fileType = 1;
  if (!original.readFrom(input, false, &fileType) ||
      original.getNumTracks() == 0 || original.getTimeFormat() == 0)
    return juce::Result::fail(
        juce::String::fromUTF8("MIDI 文件无效或没有轨道"));
  juce::MidiFile filtered;
  const auto format = original.getTimeFormat();
  if (format > 0)
    filtered.setTicksPerQuarterNote(format);
  else
    filtered.setSmpteTimeFormat(-(format >> 8), format & 0xff);
  bool changed = false;
  int melodicNotes = 0;
  for (int index = 0; index < original.getNumTracks(); ++index) {
    const auto& track = *original.getTrack(index);
    bool drumTrack = false;
    juce::Array<int> noteChannels;
    for (const auto* event : track) {
      const auto& m = event->message;
      if (m.isNoteOnOrOff()) noteChannels.addIfNotAlreadyThere(m.getChannel());
      if (m.isMetaEvent() &&
          (m.getMetaEventType() == 3 || m.getMetaEventType() == 4))
        drumTrack = drumTrack || percussionName(m.getTextFromTextMetaEvent());
    }
    // A multi-channel track can be an entire type-0 song. Its title must not
    // cause unrelated melodic channels to be discarded; channel 10 is still
    // filtered below. Names classify only a single instrument channel.
    drumTrack = drumTrack && noteChannels.size() == 1;
    juce::MidiMessageSequence retained;
    for (const auto* event : track) {
      const auto& m = event->message;
      if (m.getChannel() == 10 || (drumTrack && !m.isMetaEvent())) {
        changed = true;
        continue;
      }
      // Tempo, meter and key signatures on drum tracks still apply globally.
      if (m.isEndOfTrackMetaEvent() ||
          (drumTrack && m.isMetaEvent() &&
           (m.getMetaEventType() == 3 || m.getMetaEventType() == 4)))
        continue;
      if (m.isNoteOn()) ++melodicNotes;
      retained.addEvent(m);
    }
    if (retained.getNumEvents() > 0) filtered.addTrack(retained);
  }
  if (!changed)
    return juce::Result::ok();  // Preserve untouched files byte-for-byte.
  if (melodicNotes == 0)
    return juce::Result::fail(
        juce::String::fromUTF8("过滤打击乐后没有可用的旋律或和弦 MIDI"));
  juce::MemoryOutputStream stream;
  if (!filtered.writeTo(stream, fileType))
    return juce::Result::fail(juce::String::fromUTF8("无法保存过滤后的 MIDI"));
  bytes = stream.getMemoryBlock();
  return juce::Result::ok();
}

juce::Result DuomiClient::parseMidi(const juce::MemoryBlock& bytes,
                                    GeneratedMidi& output) {
  if (!isMidi(bytes))
    return juce::Result::fail(
        juce::String::fromUTF8("下载内容不是标准 MIDI 文件"));
  juce::MemoryInputStream input(bytes, false);
  juce::MidiFile midi;
  if (!midi.readFrom(input) || midi.getNumTracks() == 0 ||
      midi.getTimeFormat() == 0)
    return juce::Result::fail(
        juce::String::fromUTF8("MIDI 文件无效或没有轨道"));
  output.parts.clear();
  for (int t = 0; t < midi.getNumTracks(); ++t) {
    const auto& sequence = *midi.getTrack(t);
    juce::String name;
    std::array<bool, 17> channels{};
    std::array<int, 17> programs{};
    for (const auto* e : sequence) {
      const auto& m = e->message;
      if (m.isTrackNameEvent())
        name = m.getTextFromTextMetaEvent().substring(0, 100);
      if (m.isNoteOn()) channels[(size_t)m.getChannel()] = true;
      if (m.isProgramChange())
        programs[(size_t)m.getChannel()] = m.getProgramChangeNumber();
    }
    for (int ch = 1; ch <= 16; ++ch)
      if (channels[(size_t)ch]) {
        auto label = name.isNotEmpty()
                         ? name
                         : juce::String(juce::MidiMessage::getGMInstrumentName(
                               programs[(size_t)ch]));
        output.parts.push_back(
            {t, ch,
             juce::String(t + 1) + ". " + label + " / Ch " + juce::String(ch)});
      }
  }
  midi.convertTimestampTicksToSeconds();
  juce::MidiMessageSequence merged;
  for (int track = 0; track < midi.getNumTracks(); ++track)
    merged.addSequence(*midi.getTrack(track), 0.0);
  merged.updateMatchedPairs();
  juce::MidiMessageSequence clean;
  int noteCount = 0;
  double duration = 0.0;
  for (const auto* event : merged) {
    const auto& message = event->message;
    const auto start = message.getTimeStamp();
    if (!std::isfinite(start) || start < 0.0)
      return juce::Result::fail(juce::String::fromUTF8("MIDI 时间戳无效"));
    if (message.isNoteOn()) {
      auto* off = event->noteOffObject;
      if (off == nullptr || !std::isfinite(off->message.getTimeStamp()) ||
          off->message.getTimeStamp() <= start)
        continue;
      clean.addEvent(message);
      clean.addEvent(off->message);
      duration = juce::jmax(duration, off->message.getTimeStamp());
      ++noteCount;
    } else if (!message.isNoteOff() && !message.isMetaEvent() &&
               !message.isSysEx()) {
      clean.addEvent(message);
    }
  }
  if (noteCount == 0)
    return juce::Result::fail(
        juce::String::fromUTF8("MIDI 中没有可播放的完整音符"));
  clean.updateMatchedPairs();
  output.fileBytes = bytes;
  output.sequence = std::move(clean);
  output.noteCount = noteCount;
  output.durationSeconds = duration;
  return juce::Result::ok();
}

juce::Result DuomiClient::separateStems(
    const juce::String& key, Cancellation& cancel, StemBatch& batch,
    std::function<void(const juce::String&)> progress) {
  const auto report = [&](const juce::String& s) {
    if (progress) progress(s);
  };
  const auto fail = [&](const juce::String& s) {
    return juce::Result::fail(redact(s, key));
  };
  if (key.trim().isEmpty() || key.containsAnyOf("\r\n") ||
      batch.sourceTask.isEmpty())
    return fail(juce::String::fromUTF8("缺少有效 Key 或来源音乐任务"));
  if (batch.stems.empty()) {
    if (batch.submissionUncertain)
      return fail(juce::String::fromUTF8(
          "上次分轨提交结果不明，已阻止重复扣费；请先在多米账户核对任务。"));
    juce::var song;
    auto status = waitForTask(key, batch.sourceTask, cancel, song, report);
    if (status.failed()) return fail(status.getErrorMessage());
    batch.clipId = song["clip_id"].toString();
    if (batch.clipId.isEmpty())
      return fail(juce::String::fromUTF8("音乐任务没有 clip_id，无法分离乐器"));
    juce::DynamicObject::Ptr body = new juce::DynamicObject;
    body->setProperty("task", "gen_stem");
    body->setProperty("mv", "chirp-auk");
    body->setProperty("stem_task", "twelve");
    body->setProperty("continue_clip_id", batch.clipId);
    body->setProperty("custom_mode", false);
    body->setProperty("make_instrumental", false);
    if (cancel.isCancelled()) return fail(juce::String::fromUTF8("已取消"));
    batch.submissionUncertain = true;
    report(juce::String::fromUTF8("正在提交乐器分离任务（最多 12 轨）…"));
    // Never retry a billable POST automatically, including timeouts/5xx.
    auto response = request(
        {juce::URL(options.generateHosts[0] + "/api/suno/generate")
             .withPOSTData(juce::JSON::toString(juce::var(body.get()))),
         "POST",
         "Authorization: " + key + "\r\nContent-Type: application/json\r\n"},
        key, cancel);
    juce::var json;
    status = readJson(response, json);
    if (status.failed()) {
      if (response.status >= 400 && response.status < 500)
        batch.submissionUncertain = false;
      report(juce::String::fromUTF8("分轨提交未完成"));
      return fail(status.getErrorMessage());
    }
    auto data = json.hasProperty("data") ? json["data"] : json;
    juce::Array<juce::var> entries;
    if (auto* array = data.getArray())
      entries = *array;
    else
      entries.add(data);
    if (entries.isEmpty() || entries.size() > 12)
      return fail(
          juce::String::fromUTF8("分轨返回数量异常，停止提交；请核对云端任务"));
    for (const auto& item : entries) {
      const auto id = item["task_id"].toString();
      if (id.isEmpty() || id.length() > 512) continue;
      bool duplicate = false;
      for (const auto& existing : batch.stems)
        duplicate |= existing.taskId == id;
      if (!duplicate)
        batch.stems.push_back({id,
                               juce::String::fromUTF8("分轨 ") +
                                   juce::String((int)batch.stems.size() + 1),
                               {},
                               {}});
    }
    if (batch.stems.empty())
      return fail(juce::String::fromUTF8(
          "返回中缺少分轨 task_id；请核对账户，避免重复扣费"));
    batch.submissionUncertain = false;
    report(juce::String::fromUTF8("分轨已提交，正在等待结果…"));
  }
  const auto mediaUrl = [](const juce::var& item) {
    auto valid = [](const juce::String& url) {
      return url.startsWithIgnoreCase("https://") &&
             juce::URL(url).getDomain().isNotEmpty() &&
             !url.contains("/api/forbidden");
    };
    auto url = item["audio_url"].toString();
    // Verified stem response: these provider proxies expose HTTPS even when
    // the API advertises HTTP. CloudFront media encoding 1.0.0 is encrypted.
    const auto host = juce::URL(url).getDomain().toLowerCase();
    if (url.startsWithIgnoreCase("http://") &&
        (host == "cn2.m4a.bmnmny.cn" || host == "cn3.m4a.bmnmny.cn"))
      url = "https" + url.substring(4);
    if (valid(url)) return url;
    if (auto* media = item["media_urls"].getArray())
      for (const auto& candidate : *media) {
        if (candidate["encoding"].toString().isNotEmpty()) continue;
        url = candidate["url"].toString();
        if (valid(url)) return url;
      }
    return juce::String{};
  };
  const auto displayName = [](const juce::String& raw) {
    const char* ids[]{
        "Vocals",     "Backing_Vocals", "Drums", "Bass", "Guitar", "Keyboard",
        "Percussion", "Strings",        "Synth", "FX",   "Brass",  "Woodwinds"};
    const char* labels[]{"人声",   "和声",        "鼓",     "贝斯",
                         "吉他",   "键盘 / 钢琴", "打击乐", "弦乐",
                         "合成器", "音效",        "铜管",   "木管"};
    for (int i = 0; i < 12; ++i)
      if (raw.equalsIgnoreCase(ids[i]))
        return juce::String::fromUTF8(labels[i]) + " (" + raw + ")";
    return raw.substring(0, 160);
  };
  for (auto& stem : batch.stems) {
    if (stem.url.isNotEmpty() && !stem.url.contains("/api/forbidden")) continue;
    juce::var result;
    const auto status = waitForTask(key, stem.taskId, cancel, result, report);
    if (status.failed()) return fail(status.getErrorMessage());
    // Verified live: one parent task contains two 12-stem variants in data.
    // Keep the first complete version of each named instrument, not the
    // parent's forbidden placeholder URL or both duplicate variants.
    if (auto* clips = result["data"].getArray()) {
      if (clips->isEmpty() || clips->size() > 48)
        return fail(juce::String::fromUTF8("分轨列表异常，可继续查询"));
      std::vector<AudioStem> expanded;
      juce::StringArray seen;
      for (const auto& clip : *clips) {
        auto name = clip["metadata"]["stem_type_group_name"].toString();
        if (name.isEmpty()) name = clip["title"].toString();
        if (name.isEmpty() || seen.contains(name, true)) continue;
        auto url = mediaUrl(clip);
        if (url.isEmpty() || clip["status"].toString() != "complete") continue;
        seen.add(name);
        expanded.push_back({stem.taskId, displayName(name), url, {}});
      }
      if (expanded.empty() || expanded.size() > 12)
        return fail(juce::String::fromUTF8("没有可下载的乐器分轨，可继续查询"));
      batch.stems = std::move(expanded);
      report(juce::String::fromUTF8("分轨已完成，共 ") +
             juce::String((int)batch.stems.size()) +
             juce::String::fromUTF8(" 个乐器轨道"));
      return juce::Result::ok();
    }
    const auto url = mediaUrl(result);
    if (url.isEmpty())
      return fail(juce::String::fromUTF8("分轨未返回有效音频地址，可继续查询"));
    stem.url = url;
    auto name = result["stem_type"].toString();
    if (name.isEmpty()) name = result["title"].toString();
    if (name.isNotEmpty()) stem.name = displayName(name);
    report(juce::String::fromUTF8("分轨已完成：") + stem.name);
  }
  return juce::Result::ok();
}
juce::Result DuomiClient::generate(const juce::String& key,
                                   const juce::String& prompt,
                                   Cancellation& cancel, GeneratedMidi& output,
                                   Progress progress,
                                   const juce::String& existingTaskId,
                                   const juce::String& existingMidiTaskId) {
  if (key.trim().isEmpty() || key.containsAnyOf("\r\n"))
    return juce::Result::fail(juce::String::fromUTF8("请输入有效的 API Key"));
  if (existingTaskId.isEmpty() && existingMidiTaskId.isEmpty() &&
      prompt.trim().isEmpty())
    return juce::Result::fail(juce::String::fromUTF8("请先填写音乐描述"));
  output = {};
  output.taskId = existingTaskId;
  output.midiTaskId = existingMidiTaskId;
  const auto report = [&](const juce::String& text) {
    if (progress) progress(text, output.taskId);
  };
  const auto failure = [&](const juce::String& message) {
    return juce::Result::fail(redact(message, key));
  };
  const auto auth = "Authorization: " + key +
                    "\r\nContent-Type: application/json; charset=utf-8\r\n";
  if (output.taskId.isEmpty() && output.midiTaskId.isEmpty()) {
    report(juce::String::fromUTF8("生成中…正在提交音乐任务"));
    juce::var generated;
    bool submitted = false;
    const juce::StringArray models{"chirp-v3-5", "chirp-v4-5", "chirp-v4",
                                   "chirp-v3",   "chirp-auk",  "chirp-bluejay"};
    for (const auto& model : models) {
      juce::DynamicObject::Ptr body = new juce::DynamicObject;
      body->setProperty("mv", model);
      body->setProperty("custom_mode", false);
      body->setProperty("make_instrumental", true);
      body->setProperty("prompt", prompt.trim());
      auto url = juce::URL(options.generateHosts[0] + "/api/suno/generate")
                     .withPOSTData(juce::JSON::toString(juce::var(body.get())));
      auto response = request({url, "POST", auth}, key, cancel);
      // Only a definitive 404 permits an alternative host. A timeout/5xx may
      // have already created a billable task and must never be
      // auto-resubmitted.
      if (response.status == 404 && options.generateHosts.size() > 1)
        response =
            request({juce::URL(options.generateHosts[1] + "/api/suno/generate")
                         .withPOSTData(url.getPostData()),
                     "POST", auth},
                    key, cancel);
      if (invalidModel(response)) continue;
      if (auto result = readJson(response, generated); result.failed())
        return failure(result.getErrorMessage());
      output.taskId = firstData(generated)["task_id"].toString();
      if (output.taskId.isEmpty())
        return failure(juce::String::fromUTF8(
            "生成返回中缺少 task_id，任务状态不明，请检查日志后再提交"));
      submitted = true;
      break;
    }
    if (!submitted)
      return failure(
          juce::String::fromUTF8("服务不接受配置中的模型，请查看日志"));
  }
  HttpResponse response;
  if (output.midiTaskId.isEmpty()) {
    report(juce::String::fromUTF8("生成中…正在等待音乐任务完成"));
    juce::var song;
    if (auto result =
            waitForTask(key, output.taskId, cancel, song,
                        [&](const juce::String& state) {
                          report(juce::String::fromUTF8("生成中…") + state);
                        });
        result.failed())
      return failure(result.getErrorMessage());
    output.audioUrl = song["audio_url"].toString();
    report(juce::String::fromUTF8("提取 MIDI 中…"));
    juce::DynamicObject::Ptr body = new juce::DynamicObject;
    body->setProperty("task_id", output.taskId);
    for (const auto& host : options.midiHosts) {
      // Verified live: task_id is required in the JSON body of this GET.
      response = request(
          {juce::URL(host + "/api/suno/midi")
               .withParameter("key", key)
               .withPOSTData(juce::JSON::toString(juce::var(body.get()))),
           "GET", "Content-Type: application/json; charset=utf-8\r\n"},
          key, cancel);
      if (response.status != 404) break;
    }
    if (!isMidi(response.body) &&
        (response.status < 300 || response.status >= 400)) {
      juce::var json;
      if (auto result = readJson(response, json); result.failed())
        return failure(result.getErrorMessage());
      auto data = firstData(json);
      if (data.isString() && isTaskId(data.toString()))
        output.midiTaskId = data.toString();
      else if (data.hasProperty("task_id"))
        output.midiTaskId = data["task_id"].toString();
    }
  }
  if (output.midiTaskId.isNotEmpty()) {
    report(juce::String::fromUTF8("提取 MIDI 中…正在等待音符结果"));
    juce::var midiResult;
    if (auto result = waitForTask(
            key, output.midiTaskId, cancel, midiResult,
            [&](const juce::String& state) {
              report(juce::String::fromUTF8("提取 MIDI 中…") + state);
            });
        result.failed())
      return failure(result.getErrorMessage());
    const auto payload = juce::JSON::toString(midiResult["data"]);
    response = {};
    response.status = 200;
    response.contentType = "application/json";
    response.body.append(payload.toRawUTF8(), payload.getNumBytesAsUTF8());
  }
  juce::MemoryBlock bytes;
  if (auto result = decodeMidi(std::move(response), key, cancel, bytes);
      result.failed())
    return failure(result.getErrorMessage());
  report(juce::String::fromUTF8("正在检查旋律和和弦、过滤打击乐…"));
  if (auto result = filterPercussion(bytes); result.failed())
    return failure(result.getErrorMessage());
  if (auto result = parseMidi(bytes, output); result.failed())
    return failure(result.getErrorMessage());
  if (cancel.isCancelled()) return failure(juce::String::fromUTF8("已取消"));
  if (auto result = options.outputDirectory.createDirectory(); result.failed())
    return failure(juce::String::fromUTF8("无法创建 MIDI 临时目录"));
  output.file = options.outputDirectory.getChildFile(
      "duomi_" + juce::Uuid().toString() + ".mid");
  if (!output.file.replaceWithData(bytes.getData(), bytes.getSize())) {
    output.file.deleteFile();
    output.file = juce::File{};
    return failure(juce::String::fromUTF8("MIDI 写入磁盘失败"));
  }
  report(juce::String::fromUTF8("完成：") + juce::String(output.noteCount) +
         juce::String::fromUTF8(" 个音符，") +
         juce::String(output.durationSeconds, 1) +
         juce::String::fromUTF8(" 秒"));
  return juce::Result::ok();
}
}  // namespace duomi
