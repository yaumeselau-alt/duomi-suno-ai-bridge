# Duomi Suno AI Bridge — English

[简体中文](README.md) | **English**

A JUCE instrument plugin for generating music through Duomi, extracting MIDI,
previewing MIDI/audio and dragging editable clips into Ableton Live or another DAW.

## Features

- Music description and a separate BPM control: scroll to adjust or double-click to type.
- MIDI piano roll and audio waveform with bar/beat rulers, playback, pause, stop and seeking.
- Hold and drag the piano roll to a MIDI track, or the waveform to an audio track.
- MIDI instrument-track selection and paid cloud instrument separation.
- Separate editable piano block chords while keeping the original MIDI.
- Black/purple interface, project restoration and Windows-encrypted API-key storage.
- **Simplified Chinese / English** selector at the top right, available in VST3, AU and standalone builds.

The language changes immediately and is stored in the host project/standalone state.
Reopening the editor retains the choice; existing projects default to Simplified Chinese.
Each plugin instance has its own setting. Switching language does not change your
prompt, filenames, MIDI, audio, playback or submitted tasks. Native file-dialog
system buttons follow the operating system; raw service errors remain as returned.

Use your own Duomi API key. Local playback is free of cloud requests; generation,
MIDI extraction and instrument separation use your Duomi balance. Source and CI
contain no real API keys and never run paid API tests automatically.

## Build and install

```sh
git clone --recurse-submodules https://github.com/yaumeselau-alt/duomi-suno-ai-bridge.git
cd duomi-suno-ai-bridge
```

JUCE 9.0.3 is pinned to `be29c81492b6151c8ea8d14c840e1311963b3a83`.
Initialize submodules before building.

Windows 10/11 x64: install Visual Studio 2022 C++ Build Tools, a Windows SDK,
CMake 3.22+ and Inno Setup 6.7+, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/build-package.ps1
```

On a Mac with full Xcode and CMake 3.22+:

```sh
bash packaging/macos/build-package.sh
```

The target is macOS 11+ Universal (Apple Silicon and Intel), with VST3, AU and a
standalone app. Installation paths and FFmpeg setup are in
[the English installation guide](packaging/INSTALL.en.txt).

## GitHub macOS builds

Open **Actions → macOS Universal installer → Run workflow**. Relevant pushes
to main also trigger a build. It compiles both architectures, runs offline tests,
checks bundle signatures and produces a pkg. Download
**Duomi-Suno-AI-Bridge-macOS-Universal** from that run's Artifacts.
Installers are retained for 14 days; diagnostic logs for 7 days.

The first 0.0.2 Universal installer was produced in
[run 38207700838](https://github.com/yaumeselau-alt/duomi-suno-ai-bridge/actions/runs/38207700838).
**That older installer does not contain the 0.0.3 language selector.**
Use an artifact built from the current 0.0.3 source for this feature.

CI uses a standard macos-15 runner, with a 45-minute limit, no Duomi key and no
Apple certificates. It does not change your account's billing or budget settings.

## Current limits

- macOS bundles use ad-hoc signatures; the installer has no Developer ID signature
  or Apple notarization. Gatekeeper may block installation.
- Compilation, offline tests and archive verification do not replace manual
  installation and Live/AU playback/drag testing on a Mac.
- API-key persistence is currently Windows-only. Enter the key manually on a Mac.
- M4A/Opus stems need a separately installed and configured FFmpeg executable.
- Cloud separation may queue or fail. Re-querying a failed task does not restart it.
- Save your project and close DAWs before installing. Windows uninstallation
  preserves generated media, task caches and saved keys.
- The current built-in synthesizer is a preview sound, not a piano sample library.

## Documentation

| Topic | 简体中文 | English |
|---|---|---|
| Project progress | [项目规范](PROJECT_SPEC.md) | [Project specification](PROJECT_SPEC.en.md) |
| Tracks and stems | [轨道与分轨](TRACK_WORKFLOW.md) | [Tracks and stems](TRACK_WORKFLOW.en.md) |
| Block chords | [柱式和弦](CHORD_WORKFLOW.md) | [Block chords](CHORD_WORKFLOW.en.md) |
| Build and packaging | [打包](packaging/README.md) | [Packaging](packaging/README.en.md) |
| Installation | [安装](packaging/INSTALL.txt) | [Installation](packaging/INSTALL.en.txt) |
| Tests | [验证](tests/README.md) | [Validation](tests/README.en.md) |
| Changes | [更新记录](CHANGELOG.zh-CN.md) | [Changelog](CHANGELOG.md) |
| Third-party notices | [依赖说明](packaging/THIRD_PARTY_NOTICES.zh-CN.txt) | [Notices](packaging/THIRD_PARTY_NOTICES.txt) |

Derived from tomoyanonymous/juce_cmake_vscode_example. Original third-party
license texts are retained; translations do not replace those license texts.

