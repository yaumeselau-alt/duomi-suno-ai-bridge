#include "AudioPreviewPlayer.h"

#include <cmath>
#include <limits>

#include "ContainerAudioDecoder.h"

namespace duomi {
namespace {
constexpr size_t maxDownload = 64 * 1024 * 1024;
constexpr juce::int64 maxPcmBytes = 128 * 1024 * 1024;
juce::String tr(const char* text) { return juce::String::fromUTF8(text); }
bool validUrl(const juce::URL& url) {
  return url.getScheme().equalsIgnoreCase("https") &&
         url.getDomain().isNotEmpty();
}
}  // namespace
AudioPreviewPlayer::AudioPreviewPlayer(DuomiClient::Transport custom,
                                       juce::File directory)
    : Thread("Duomi audio preview"),
      transport(custom ? std::move(custom) : downloadNative),
      exportDirectory(std::move(directory)) {
  snapshot.message = tr("生成 MIDI 后可加载成品音频，也可打开本地音频。");
}
AudioPreviewPlayer::~AudioPreviewPlayer() {
  cancel();
  waitForThreadToExit(-1);
  delete pending.exchange(nullptr);
  collectRetired();
  delete active;
}
void AudioPreviewPlayer::collectRetired() {
  auto* clip = retired.exchange(nullptr);
  while (clip) {
    auto* next = clip->next;
    delete clip;
    clip = next;
  }
}
bool AudioPreviewPlayer::download(juce::String url) {
  return start(std::move(url), {});
}
bool AudioPreviewPlayer::importFile(juce::File file) {
  return start({}, std::move(file));
}
bool AudioPreviewPlayer::start(juce::String url, juce::File file) {
  if (isThreadRunning()) return false;
  collectRetired();
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (snapshot.busy) return false;
    if (file == juce::File{} && !validUrl(juce::URL(url))) {
      snapshot.failed = true;
      snapshot.message =
          tr("没有有效的 HTTPS 成品音频地址，请先完成音乐生成。");
      return false;
    }
    if (url.isNotEmpty() && snapshot.ready && snapshot.source == url &&
        snapshot.dragFile.existsAsFile()) {
      snapshot.failed = false;
      snapshot.message = tr("成品音频已就绪，可直接播放。");
      return true;
    }
    requestedUrl = std::move(url);
    requestedFile = std::move(file);
    cancellation = std::make_shared<Cancellation>();
    snapshot.busy = true;
    snapshot.failed = false;
    snapshot.message =
        requestedUrl.isEmpty() ? tr("正在读取音频…") : tr("正在下载成品音频…");
  }
  if (!startWorker()) {
    std::lock_guard<std::mutex> lock(mutex);
    snapshot.busy = false;
    snapshot.failed = true;
    snapshot.message = tr("无法启动音频后台线程，请稍后重试。");
    return false;
  }
  return true;
}
void AudioPreviewPlayer::clear() {
  if (isThreadRunning()) return;
  stop();
  auto empty = std::make_unique<Clip>();
  empty->sampleRate = 1.0;
  delete pending.exchange(empty.release());
  std::lock_guard<std::mutex> lock(mutex);
  snapshot = {};
  snapshot.message = tr("尚未加载音频");
}
void AudioPreviewPlayer::cancel() {
  std::shared_ptr<Cancellation> token;
  {
    std::lock_guard<std::mutex> lock(mutex);
    token = cancellation;
  }
  if (token) token->cancel();
}
HttpResponse AudioPreviewPlayer::downloadNative(const HttpRequest& request,
                                                Cancellation& cancel) {
  HttpResponse response;
  auto stream = std::make_shared<juce::WebInputStream>(request.url, false);
  stream->withConnectionTimeout(15000).withNumRedirectsToFollow(0);
  cancel.setStream(stream);
  const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120000;
  if (!cancel.isCancelled() && stream->connect(nullptr)) {
    response.status = stream->getStatusCode();
    response.location = stream->getResponseHeaders()["Location"];
    if (response.status == 200) {
      char buffer[16384];
      while (!cancel.isCancelled() && !stream->isExhausted()) {
        if (juce::Time::getMillisecondCounterHiRes() > deadline) {
          response.error = tr("音频下载超时，请重试。");
          break;
        }
        const auto count = stream->read(buffer, sizeof(buffer));
        if (count <= 0) break;
        if (response.body.getSize() + static_cast<size_t>(count) >
            maxDownload) {
          response.error = tr("音频文件超过 64 MB 限制。");
          break;
        }
        response.body.append(buffer, static_cast<size_t>(count));
      }
      if (stream->isError() && response.error.isEmpty())
        response.error = tr("音频下载中断。");
      const auto expected = stream->getTotalLength();
      if (expected > 0 &&
          expected != static_cast<juce::int64>(response.body.getSize()) &&
          response.error.isEmpty())
        response.error = tr("音频下载不完整，请重试。");
    }
  } else
    response.error = tr("无法连接音频下载地址，请检查网络后重试。");
  cancel.setStream(nullptr);
  return response;
}
void AudioPreviewPlayer::run() {
  const auto token = cancellation;
  juce::String error, dragError;
  juce::File dragFile;
  std::unique_ptr<Clip> clip;
  std::shared_ptr<Waveform> waveform;
  try {
    juce::MemoryBlock bytes;
    if (requestedUrl.isNotEmpty()) {
      auto url = juce::URL(requestedUrl);
      for (int redirects = 0;; ++redirects) {
        if (token->isCancelled()) break;
        if (!validUrl(url)) {
          error = tr("音频重定向必须使用 HTTPS。");
          break;
        }
        // CDN requests never carry the Duomi API key or Authorization header.
        auto response = transport({url, "GET", {}}, *token);
        if (response.error.isNotEmpty()) {
          error = response.error;
          break;
        }
        if (response.status == 301 || response.status == 302 ||
            response.status == 303 || response.status == 307 ||
            response.status == 308) {
          if (redirects >= 3 || response.location.isEmpty()) {
            error = tr("音频重定向次数过多或地址无效。");
            break;
          }
          const auto location = response.location;
          const auto base =
              url.toString(false).upToFirstOccurrenceOf("?", false, false);
          const auto authority =
              base.upToFirstOccurrenceOf("://", true, false) +
              base.fromFirstOccurrenceOf("://", false, false)
                  .upToFirstOccurrenceOf("/", false, false);
          if (location.contains("://"))
            url = juce::URL(location);
          else if (location.startsWith("//"))
            url = juce::URL("https:" + location);
          else if (location.startsWith("/"))
            url = juce::URL(authority + location);
          else if (location.startsWith("?"))
            url = juce::URL(base + location);
          else
            url = juce::URL(base.upToLastOccurrenceOf("/", true, false) +
                            location);
          continue;
        }
        if (response.status != 200) {
          error = tr("音频下载失败，HTTP ") + juce::String(response.status) +
                  tr("；地址可能已过期。");
          break;
        }
        bytes = std::move(response.body);
        break;
      }
    } else {
      if (!requestedFile.existsAsFile() || requestedFile.getSize() <= 0 ||
          requestedFile.getSize() > static_cast<juce::int64>(maxDownload))
        error = tr("音频文件不存在、为空或超过 64 MB。");
      else if (!requestedFile.loadFileAsData(bytes))
        error = tr("无法读取音频文件。");
    }
    if (error.isEmpty() && !token->isCancelled()) {
      if (bytes.getSize() == 0 || bytes.getSize() > maxDownload)
        error = tr("音频文件为空或超过 64 MB。");
      else {
        {
          std::lock_guard<std::mutex> lock(mutex);
          snapshot.message = tr("正在解码音频…");
        }
        juce::MemoryBlock decoded;
        if (ContainerAudioDecoder::isIsoMedia(bytes)) {
          auto result = ContainerAudioDecoder::decode(bytes, decoded, *token);
          if (result.failed())
            error = result.getErrorMessage();
          else
            bytes.swapWith(decoded);
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto input = std::make_unique<juce::MemoryInputStream>(bytes, false);
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(std::move(input)));
        if (error.isNotEmpty()) {
        } else if (!reader)
          error = tr("音频格式不受支持或文件已损坏（支持 MP3/WAV/FLAC）。");
        else if (!std::isfinite(reader->sampleRate) ||
                 reader->sampleRate < 8000 || reader->sampleRate > 192000 ||
                 reader->numChannels < 1 || reader->numChannels > 2 ||
                 reader->lengthInSamples <= 0 ||
                 reader->lengthInSamples > std::numeric_limits<int>::max() ||
                 reader->lengthInSamples / reader->sampleRate > 600 ||
                 reader->lengthInSamples >
                     maxPcmBytes / (sizeof(float) * reader->numChannels))
          error = tr(
              "音频超出预览限制：单/双声道、最长 10 分钟、解码后最多 128 MB。");
        else {
          clip = std::make_unique<Clip>();
          clip->sampleRate = reader->sampleRate;
          clip->samples.setSize(static_cast<int>(reader->numChannels),
                                static_cast<int>(reader->lengthInSamples));
          for (int start = 0;
               start < clip->samples.getNumSamples() && !token->isCancelled();
               start += 16384) {
            const int count =
                juce::jmin(16384, clip->samples.getNumSamples() - start);
            if (!reader->read(&clip->samples, start, count, start, true,
                              true)) {
              error = tr("音频解码失败。");
              break;
            }
            for (int ch = 0; ch < clip->samples.getNumChannels(); ++ch)
              for (int i = start; i < start + count; ++i) {
                const float v = clip->samples.getSample(ch, i);
                clip->samples.setSample(
                    ch, i,
                    std::isfinite(v) ? juce::jlimit(-1.0f, 1.0f, v) : 0.0f);
              }
          }
        }
      }
    }
    if (clip && error.isEmpty() && !token->isCancelled()) {
      waveform = std::make_shared<Waveform>();
      const int samples = clip->samples.getNumSamples();
      const int bins = juce::jmin(2048, samples);
      waveform->durationSeconds = samples / clip->sampleRate;
      waveform->peaks.resize(static_cast<size_t>(bins));
      for (int bin = 0; bin < bins && !token->isCancelled(); ++bin) {
        const int start =
            static_cast<int>(static_cast<juce::int64>(bin) * samples / bins);
        const int end = static_cast<int>(static_cast<juce::int64>(bin + 1) *
                                         samples / bins);
        auto& peak = waveform->peaks[static_cast<size_t>(bin)];
        for (int ch = 0; ch < clip->samples.getNumChannels(); ++ch)
          for (int i = start; i < end; ++i) {
            const auto value = clip->samples.getSample(ch, i);
            peak.low = juce::jmin(peak.low, value);
            peak.high = juce::jmax(peak.high, value);
          }
      }
    }
    if (clip && error.isEmpty() && !token->isCancelled()) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot.message = tr("正在准备可拖入 DAW 的音频文件…");
      }
      if (exportDirectory.createDirectory().failed())
        dragError = tr("可试听，但无法创建音频导出目录，请检查磁盘权限。");
      else {
        const auto name = requestedFile == juce::File{}
                              ? juce::String("Duomi Audio")
                              : requestedFile.getFileNameWithoutExtension();
        const auto destination = exportDirectory.getChildFile(
            juce::File::createLegalFileName(name) + "-" +
            juce::Uuid().toString() + ".wav");
        juce::TemporaryFile temporary(destination);
        auto stream = temporary.getFile().createOutputStream();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            stream ? format.createWriterFor(
                         stream.get(), clip->sampleRate,
                         static_cast<unsigned>(clip->samples.getNumChannels()),
                         24, {}, 0)
                   : nullptr);
        if (writer) stream.release();
        bool ok = writer != nullptr;
        for (int start = 0; ok && start < clip->samples.getNumSamples() &&
                            !token->isCancelled();
             start += 16384)
          ok = writer->writeFromAudioSampleBuffer(
              clip->samples, start,
              juce::jmin(16384, clip->samples.getNumSamples() - start));
        writer.reset();
        if (!token->isCancelled()) {
          if (ok && temporary.overwriteTargetFileWithTemporary())
            dragFile = destination;
          else
            dragError =
                tr("可试听，但无法写入拖拽 WAV，请检查磁盘空间后重新加载。");
        }
      }
    }
  } catch (...) {
    error = tr("音频加载失败，内存不足或文件无效。");
  }
  std::lock_guard<std::mutex> lock(mutex);
  if (token->isCancelled()) {
    if (dragFile != juce::File{}) dragFile.deleteFile();
    snapshot.failed = false;
    snapshot.message = tr("已取消音频加载；原有预览仍可播放。");
  } else if (error.isNotEmpty() || !clip) {
    snapshot.failed = true;
    snapshot.message = error.isNotEmpty() ? error : tr("音频加载失败。");
  } else {
    stop();
    delete pending.exchange(clip.release());
    snapshot.waveform = std::move(waveform);
    snapshot.dragFile = dragFile;
    snapshot.ready = true;
    snapshot.failed = false;
    snapshot.source = requestedUrl.isNotEmpty()
                          ? requestedUrl
                          : requestedFile.getFullPathName();
    snapshot.failed = dragError.isNotEmpty();
    snapshot.message =
        dragError.isNotEmpty()
            ? dragError
            : tr("成品音频已就绪：可试听，或按住波形拖入音频轨道。");
  }
  snapshot.busy = false;
}
void AudioPreviewPlayer::requestState(unsigned requested) {
  auto old = command.load();
  for (;;) {
    const auto reset = old != appliedCommand.load() ? old & 4u : 0u;
    if (command.compare_exchange_weak(old,
                                      ((old + 8u) & ~7u) | reset | requested))
      break;
  }
}
void AudioPreviewPlayer::play() { requestState(1); }
void AudioPreviewPlayer::pause() { requestState(2); }
void AudioPreviewPlayer::stop() {
  pendingSeek.store(-1.0);
  requestState(4);
}
void AudioPreviewPlayer::seek(double fraction) {
  if (std::isfinite(fraction))
    pendingSeek.store(juce::jlimit(0.0, 1.0, fraction));
}
AudioPreviewPlayer::Snapshot AudioPreviewPlayer::getSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex);
  auto result = snapshot;
  result.busy |= isThreadRunning();
  result.state = publishedState.load();
  result.positionSeconds = publishedPosition.load();
  result.durationSeconds = result.waveform ? result.waveform->durationSeconds
                                           : publishedDuration.load();
  return result;
}
void AudioPreviewPlayer::silenceFade() {
  for (int ch = 0; ch < 2; ++ch) fadeOrigin[ch] = last[ch];
  fadeRemaining = fadeSamples;
  gain = 0;
}
void AudioPreviewPlayer::publish() {
  publishedState.store(state);
  publishedPosition.store(active ? position / active->sampleRate : 0.0);
  publishedDuration.store(
      active ? active->samples.getNumSamples() / active->sampleRate : 0.0);
}
void AudioPreviewPlayer::prepare(double rate) {
  pendingSeek.store(-1.0);
  outputRate = std::isfinite(rate) && rate > 0 ? rate : 0;
  fadeSamples = juce::jmax(1, static_cast<int>(outputRate * 0.005));
  position = 0;
  state = State::stopped;
  gain = 0;
  fadeRemaining = 0;
  last[0] = last[1] = 0;
  appliedCommand.store(command.load());
  publish();
}
void AudioPreviewPlayer::release() { prepare(0); }
void AudioPreviewPlayer::render(juce::AudioBuffer<float>& buffer) {
  buffer.clear();
  if (outputRate <= 0 || buffer.getNumSamples() == 0 ||
      buffer.getNumChannels() == 0)
    return;
  bool adopted = false;
  if (auto* next = pending.exchange(nullptr)) {
    if (active) {
      auto* head = retired.load();
      do {
        active->next = head;
      } while (!retired.compare_exchange_weak(head, active));
    }
    active = next;
    position = 0;
    state = State::stopped;
    silenceFade();
    adopted = true;
  }
  const auto latest = command.load();
  const auto control =
      adopted || latest != appliedCommand.load() ? latest & 7u : 0u;
  appliedCommand.store(latest);
  if (control & 4u) {
    position = 0;
    state = State::stopped;
    silenceFade();
  }
  if ((control & 3u) == 1 && active && active->samples.getNumSamples() > 0) {
    if (state == State::finished) position = 0;
    if (state != State::playing) gain = 0;
    state = State::playing;
    fadeRemaining = 0;
  } else if ((control & 3u) == 2 && state == State::playing) {
    state = State::paused;
    silenceFade();
  }
  const auto seekTo = pendingSeek.exchange(-1.0);
  if (seekTo >= 0.0 && active) {
    position = seekTo * active->samples.getNumSamples();
    silenceFade();
    if (position >= active->samples.getNumSamples())
      state = State::finished;
    else if (state != State::playing)
      state = State::paused;
    else
      fadeRemaining = 0;
  }
  for (int i = 0; i < buffer.getNumSamples(); ++i) {
    float values[2]{};
    if (state == State::playing && active) {
      const auto length = active->samples.getNumSamples();
      const int a = juce::jmin(static_cast<int>(position), length - 1),
                b = juce::jmin(a + 1, length - 1);
      const float fraction = static_cast<float>(position - a);
      gain = juce::jmin(1.0f, gain + 1.0f / fadeSamples);
      const float endGain = static_cast<float>(
          juce::jmin(1.0, (length - position) / (active->sampleRate * 0.005)));
      // Bounded decoded PCM keeps disk, decoder locks and allocation off this
      // thread.
      for (int ch = 0; ch < 2; ++ch) {
        const auto* samples = active->samples.getReadPointer(
            juce::jmin(ch, active->samples.getNumChannels() - 1));
        values[ch] = (samples[a] + fraction * (samples[b] - samples[a])) *
                     gain * endGain;
      }
      position = juce::jmin(static_cast<double>(length),
                            position + active->sampleRate / outputRate);
      if (position >= length) {
        state = State::finished;
      }
    } else if (fadeRemaining > 0) {
      const float factor = static_cast<float>(--fadeRemaining) / fadeSamples;
      for (int ch = 0; ch < 2; ++ch) values[ch] = fadeOrigin[ch] * factor;
    }
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
      buffer.setSample(ch, i,
                       buffer.getNumChannels() == 1
                           ? (values[0] + values[1]) * 0.5f
                           : values[juce::jmin(ch, 1)]);
    last[0] = values[0];
    last[1] = values[1];
  }
  publish();
}
}  // namespace duomi