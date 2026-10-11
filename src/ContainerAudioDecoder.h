#pragma once
#include "DuomiClient.h"
namespace duomi {
// Some current Suno stems are Opus in an MP4/M4A container. The Windows legacy
// reader recognises the container but cannot decode it. Use a configured local
// FFmpeg helper only for ISO media containers; never execute a shell or let the
// decoder fetch URLs. Temporary source/output files are scoped to this worker.
struct ContainerAudioDecoder {
  static bool isIsoMedia(const juce::MemoryBlock& bytes) {
    return bytes.getSize() >= 12 &&
           std::memcmp((const char*)bytes.getData() + 4, "ftyp", 4) == 0;
  }
  static juce::File configFile() {
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("DuomiSunoAIBridge")
        .getChildFile("ffmpeg-path.txt");
  }
  static juce::Result decode(const juce::MemoryBlock& bytes,
                             juce::MemoryBlock& wav, Cancellation& token) {
    const auto path = configFile().loadFileAsString().trim();
    if (!juce::File::isAbsolutePath(path) || !juce::File(path).existsAsFile())
      return juce::Result::fail(juce::String::fromUTF8(
          "此分轨为 M4A/Opus，未找到配置的 FFmpeg 解码器。"));
    juce::TemporaryFile input(".m4a"), output(".wav");
    if (!input.getFile().replaceWithData(bytes.getData(), bytes.getSize()))
      return juce::Result::fail(
          juce::String::fromUTF8("无法准备音频解码临时文件"));
    juce::ChildProcess process;
    juce::StringArray arguments{path,
                                "-nostdin",
                                "-v",
                                "quiet",
                                "-y",
                                "-protocol_whitelist",
                                "file,pipe",
                                "-i",
                                input.getFile().getFullPathName(),
                                "-map",
                                "0:a:0",
                                "-vn",
                                "-t",
                                "601",
                                "-c:a",
                                "pcm_s24le",
                                output.getFile().getFullPathName()};
    if (!process.start(arguments, 0))
      return juce::Result::fail(juce::String::fromUTF8("无法启动音频解码器"));
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120000;
    while (process.isRunning()) {
      if (token.isCancelled() ||
          juce::Time::getMillisecondCounterHiRes() > deadline ||
          output.getFile().getSize() > 128 * 1024 * 1024) {
        process.kill();
        process.waitForProcessToFinish(3000);
        return juce::Result::fail(
            juce::String::fromUTF8("音频解码已取消或超出时长/大小限制"));
      }
      token.wait(20);
    }
    if (process.getExitCode() != 0 || output.getFile().getSize() <= 44 ||
        output.getFile().getSize() > 128 * 1024 * 1024 ||
        !output.getFile().loadFileAsData(wav))
      return juce::Result::fail(juce::String::fromUTF8("M4A 音频解码失败"));
    return juce::Result::ok();
  }
};
}  // namespace duomi