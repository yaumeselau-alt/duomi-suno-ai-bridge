# Duomi Suno AI Bridge — public project specification

This is the repository edition. Local diagnostic logs, real task identifiers,
account information and personal generated audio are excluded.

## Implemented

- JUCE 9.0.3 C++17 instrument plugin, version 0.0.2, VST3 and standalone;
  AU is configured for macOS.
- Duomi music generation, asynchronous MIDI extraction and resumable task queries.
- Piano roll, audio waveform, playback controls and drag-to-DAW MIDI/WAV export.
- BPM input, bar/beat rulers, black/purple theme, generated MIDI percussion filtering.
- Original MIDI plus separately editable piano block chords; continuity smoothing.
- MIDI track selection and cloud instrument stems with persistent task caching.
- Host session-state restoration; Windows DPAPI key storage.

## Validation

Windows Release: 9 CTest suites pass, including actual VST3 factory, MIDI input
bus, stereo audio processing and state round-trip checks. The Windows installer
passed isolated installation, custom-folder upgrade and uninstallation checks.
Earlier Windows Live playback/dragging was accepted by the project owner.
The Mac workflow must still build and validate the Universal installer; CI does
not substitute for manual Live/AU playback and drag testing.

## Build

JUCE is pinned to be29c81492b6151c8ea8d14c840e1311963b3a83 (9.0.3).
Clone with --recurse-submodules. See packaging/README.md for Windows and Mac.
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
