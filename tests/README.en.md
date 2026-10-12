# Validation — English

[简体中文 / historical development record](README.md) | **English**

This page describes reproducible checks and the current validation boundary.
The Chinese development record also retains dated intermediate results; those
older failures and pending steps do not override the latest project specification.

## Build and run

```powershell
cmake -S . -B build-package -DDUOMI_BUILD_TESTS=ON
cmake --build build-package --config Release -- /m:2 /p:LinkIncremental=false
ctest --test-dir build-package -C Release --output-on-failure
```

Use the existing generator/configuration for a configured build folder.
Normal tests are offline: fake keys, mocked transport and local fixtures only.
Never submit a real billable task from CTest or CI.

Version 0.0.3 adds **LocalisationOffline** to the existing suites:

| Entry | Coverage |
|---|---|
| DuomiClientOffline | Generation/query/extraction protocol, JSON/binary/HTTPS/base64 MIDI, authentication, model fallback, cancellation, timeout/resume, no automatic duplicate submissions, percussion filtering |
| MidiPreviewOffline | Sample-accurate scheduling, sample rates/block sizes, pause/stop/end, voices, sustain, replacement/unload, mono/stereo and Processor playback |
| MidiTransferOffline | Unicode/spaced paths, byte-preserving export, overwrite/failures, injected drag gestures/copy flags/file ownership and callback lifetime |
| AudioPreviewOffline | Download/redirects, decode/resample, old-preview retention on failure, cancellation, waveform peaks/seeking, click-versus-drag, persistent 24-bit WAV and routing |
| ApiKeyStoreOffline | Windows DPAPI persistence, replacement/forget, corrupt/oversized keys, permissions/lifecycle and invalid-input rejection without network |
| ChordExtractionOffline | Known harmony/arpeggios, simultaneous notes, evidence limits, merging/continuity, rests/type changes, cancellation and mode selection |
| SessionStateOffline | Embedded MIDI, BPM/prompt/interval/mode, queued restores, no auto-network/playback, missing audio paths, retained task state and host dirty notifications |
| TrackSelectionOffline | Track/channel extraction, tempo/SMPTE/controllers, latest request, full mix, stem protocol, cache/duplicates/resume and project restore |
| LocalisationOffline | Catalogue, dynamic statuses, untouched Unicode paths/service text, legacy/invalid state, live switching, instance isolation, reopen/restore and UI snapshots |
| Vst3BinaryBusCheck | Windows-only exported VST3 factory, event input bus, stereo output, processing and state ABI |

ApiKeyStoreOffline has no macOS assertions. Do not count it as Mac key-storage
validation. Vst3BinaryBusCheck is Windows-only.

Run one suite with DuomiBridgeCheck --preview-tests / --transfer-tests /
--audio-tests / --key-tests / --chord-tests / --session-tests / --track-tests /
--language-tests. Language screenshots are written to language-preview in the
test working directory.

## Local media and UI checks

Existing render probes accept local files and use the plugin's Processor and
audio callback. They are not evidence of actual sound-device listening or native
DAW drag acceptance. Example:

```powershell
& './build-package/src/Release/DuomiBridgeCheck.exe' --render-preview 'input.mid' 'preview.wav'
& './build-package/src/Release/DuomiBridgeCheck.exe' --render-audio 'input.wav' 'audio-preview.wav'
```

Probe output paths may be overwritten; use dedicated validation files.
Audio render can also accept an existing HTTPS URL, which downloads audio but
does not submit generation. No real keys or personal task IDs belong in source,
command-line arguments or committed fixtures.

The older MIDI example contained 843 notes over 185.427 seconds; offline MIDI
rendering produced 185.557 seconds including its tail. Actual generated audio and
a keys/piano stem were also fully decoded/rendered. These are historical checks,
not a claim to have listened to every chord or stem.

## Explicit live probes

Real requests require user authorization to send a key to Duomi and incur charges:

- --live: submit one music generation, then extract MIDI.
- --resume TASK_ID: skip generation, query music and request extraction, which
  may itself incur a charge.
- --resume-midi MIDI_TASK_ID: only query an already accepted extraction and
  convert its notes locally.

Supply the key as one line on standard input. Never embed it in source/scripts
or command-line parameters. A generation/extraction timeout or server error is
not automatically resent. Cancelling waiting does not cancel cloud work or refund it.

Observed MIDI submission uses GET with a JSON body; it returns a new extraction
task ID. Feed notes have pitch/start/end/velocity, in seconds with 0–1 velocity.
Conversion uses format-1 MIDI, 960 PPQ and 120 BPM as a seconds-to-ticks reference,
not inferred song tempo. New generated MIDI excludes percussion.

The optional python tests/native_http_check.py probe uses loopback and fake keys.
The historical Windows WinINet loopback attempt failed with error 1168 and is
not counted as passing. Native cloud generation/query/conversion worked; the
remaining historical native extraction-submission boundary remains documented.

## Manual acceptance

Use a saved test project. Load the new plugin, select English/Chinese, reopen its
window, save/reopen the project, and confirm language and existing material.
Check both language menus, tooltips, key messages, preview/track/chord selection,
piano-roll/waveform captions and transport. User text and playback should remain unchanged.

Preview known local MIDI/audio; pause/resume/stop/seek. Drag the piano roll to a
MIDI track and waveform to an audio track. Confirm actual clips/notes/audio and
retained source files. Export MIDI and verify it independently. Mouse injection
and offline rendering alone do not complete these host checks.

Earlier Windows Live dragging/listening was accepted by the owner. Mac manual
installation, AU scanning, Live playback and dragging remain outstanding.
See [project status](../PROJECT_SPEC.en.md) for version-specific results.

