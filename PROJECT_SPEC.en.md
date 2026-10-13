# Duomi Suno AI Bridge — project specification (English)

[简体中文](PROJECT_SPEC.md) | **English**

This is the repository edition. Local diagnostic logs, real task identifiers,
account information and personal generated audio are excluded.

## Implemented

- JUCE 9.0.3 C++17 instrument plugin, version 0.0.4, VST3 and standalone;
  AU is configured for macOS.
- Duomi music generation, asynchronous MIDI extraction and resumable task queries.
- Piano roll, audio waveform, playback controls and drag-to-DAW MIDI/WAV export.
- BPM input, bar/beat rulers, black/purple theme, generated MIDI percussion filtering.
- Original MIDI plus separately editable piano block chords; continuity smoothing.
- MIDI track selection and cloud instrument stems with persistent task caching.
- Host session-state restoration; Windows DPAPI key storage.
- Per-instance Simplified Chinese / English UI selection with immediate updates,
  stored in host/standalone state. Existing projects default to Chinese. User
  prompts, file paths, source media and raw service errors are not rewritten.

## Validation

Windows 0.0.4 Release: 10 CTest entries pass, including actual VST3 factory, MIDI input
bus, stereo audio processing and state round-trip checks. The earlier 0.0.2 Windows installer
passed isolated installation, custom-folder upgrade and uninstallation checks.
Earlier Windows Live playback/dragging was accepted by the project owner.
The earlier 0.0.2 macOS Universal compilation succeeded on October 12, 2026. All 8 CTest entries
passed; ApiKeyStoreOffline has no macOS assertions, so these represent 7 effective
cross-platform suites. VST3, AU and standalone each passed arm64/x86_64 and strict
ad-hoc signature checks. The pkg was generated and expanded successfully.
Recovery run 38207700838 reused tested bundles from run 38187749423 (source
0018d984d137a79017f191de05d3622b5d30ca94), after verifying the artifact SHA256
and that compiled source files had not changed.
The downloaded artifact passed ZIP CRC and package checksum verification.
Manual Mac installation and Live/AU playback/drag testing remain outstanding.

Installer: Duomi-Suno-AI-Bridge-0.0.2-macOS-Universal-UNSIGNED.pkg
SHA256: 8986907c2007620fef48cbc9983d4df5eb892520899a8a4bb8a48184d16c3ae3
Build and download: https://github.com/yaumeselau-alt/duomi-suno-ai-bridge/actions/runs/38207700838

## Language release

Version 0.0.3 adds LocalisationOffline coverage for the catalogue, dynamic
statuses, opaque values, legacy/invalid state, live switching, instance isolation,
editor reopen and host restoration. Chinese and English screenshots are checked.
Earlier 0.0.2 installers do not contain the selector. Current Mac verification
must use a fresh build of this source, not the earlier recovery artifact.

## Build

JUCE is pinned to be29c81492b6151c8ea8d14c840e1311963b3a83 (9.0.3).
Clone with --recurse-submodules. See packaging/README.en.md for Windows and Mac.
.github/workflows/macos-package.yml builds an unsigned Universal pkg on a
standard GitHub macos-15 runner, without API keys or Apple certificates.

## Known limitations

- Mac API-key persistence is not implemented; enter the key for each session.
- M4A/Opus stem playback requires a separately configured FFmpeg executable.
- Cloud queue delays and terminal task timeouts are distinct from local waiting
  timeouts. Re-querying a failed task does not restart it.
- Signed and notarized Mac distribution requires the owner's Developer ID setup.
- Windows packages are currently unsigned.

## Development requirements

Keep HTTP, file IO and decoding off the audio callback. Never commit API keys,
DPAPI files, private media or raw diagnostic logs. Offline tests must never submit
billable cloud tasks. Preserve original MIDI and mixed audio when selecting tracks.
Update this specification and the user-facing workflow documentation with changes.

## 0.0.4 branding — 2026-10-13

Added an original purple D/audio-bar logo, editor header branding, native application icons, Windows Setup/uninstall/wizard graphics and a Mac installer welcome page. Master PNG and ICO are in assets, with the generation prompt and provenance in assets/BRAND.en.md. Installer version is 0.0.4; earlier packages do not include this branding. Mac manual installation and host validation remain outstanding.
