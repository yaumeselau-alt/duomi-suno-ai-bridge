# Track selection and instrument stems — English

[简体中文](TRACK_WORKFLOW.md) | **English**

The track selector is to the right of the preview-mode selector. The selected
track controls playback, the piano roll/waveform, dragging and MIDI export.
Original material is retained.

## MIDI preview

Open or generate MIDI, then select **All MIDI tracks** or an existing instrument
track. Tracks are listed by physical track and note channel; multi-channel
format-0 MIDI can also be separated. Metadata-only tracks are not instruments.

A worker creates an independent .mid file, preserving global tempo/time/key
signatures from all tracks and the selected channel's notes, controllers, pitch
bend and program. Leading silence and the PPQ/SMPTE timebase remain unchanged.
Files are stored in DuomiSunoAIBridge/MidiTracks under the user application-data
directory. Generated MIDI retains its percussion-filtering rules; manually
imported MIDI is not additionally rewritten.

A single piano track remains one track; pitches are not misrepresented as
separately extracted melody, harmony or instruments.

## Generated audio

1. Click **Load generated audio** to associate the current generated song.
   Do this once after reopening an older project if needed; it does not generate again.
2. Choose **Separate instruments (paid)** in the track selector.
3. When ready, choose an actual returned instrument. Its first selection downloads
   and prepares WAV; preview it or drag the waveform to a Live audio track.
4. Choose **Full mix** to return to the complete song. Each mode retains its selection.
5. After local cancellation or waiting timeout, use **Resume stem query** to query
   the existing task. Completed same-song tasks are reused from local cache.
   A server-side `task timeout` is a terminal task failure; querying does not
   restart separation. Uncertain submissions block automatic resubmission to
   avoid duplicate charges. Check the task in your Duomi account.

The implementation uses `gen_stem`, `stem_task=twelve`, `chirp-auk` and the source
song's `clip_id`, based on the [Duomi generation/stem documentation](https://duomiapi.com/doc/28).
The configured workflow describes separation as 5 times the base generation price;
the earlier documented reference was about CNY 1.70 per task, not a current price
guarantee. Check your account for actual charges. Queries use the
[feed endpoint](https://duomiapi.com/doc/31). Keys are never sent to the audio CDN.

The observed response groups all stems under one parent task. It contained two
versions of each of 12 instrument types; the plugin picks the first complete,
downloadable version of each type. Possible names include vocals, backing vocals,
drums, bass, guitar, keys/piano, percussion, strings, synth, effects, brass and
woodwinds. Actual results vary. Some stems may be silent or contain bleed;
separation does not recover original recording-session tracks.

**Open audio** loads local mixed audio without a Duomi song ID. It supports
playback, seeking and full-audio dragging, but cannot submit cloud separation.
Local mixed-audio upload and a local AI separation model are not implemented.

## Block chords

The track selector displays **Piano block chords (fixed)** and is disabled.
Analysis always uses the complete original MIDI, regardless of the MIDI track
currently selected for preview.

## State, cache and decoding

- Host state retains MIDI/audio selections, original MIDI bytes, full-audio path,
  stem tasks, URLs and downloaded WAV paths. Restore reads local files only:
  no automatic submission or download. If a stem file is missing, switch to Full
  mix and select the stem again to download it.
- Stem tasks and logs live in DuomiSunoAIBridge/StemTasks. Atomic writes and a
  per-song cross-instance lock prevent accidental duplicate submissions.
  Caches contain no API key. Successful WAV files remain in AudioExports after
  closing the plugin; include them when archiving or transferring a project.
- Observed stems used M4A/Opus. The plugin prefers decodable HTTPS proxy URLs;
  encoded CloudFront media URLs are not assumed to be ordinary M4A.
- M4A containers use a separately installed FFmpeg, followed by the existing
  background player to prepare 24-bit WAV. On Windows, configure an absolute
  executable path in %APPDATA%/DuomiSunoAIBridge/ffmpeg-path.txt using the supplied
  configuration tool. On Mac, use packaging/macos/configure-ffmpeg.sh.
- FFmpeg receives a downloaded temporary file, no URL or API key, and is launched
  without a shell in a hidden process. It supports cancellation and is limited
  to 120 seconds and 128 MB output. Existing duration/PCM limits still apply.
  [FFmpeg](https://ffmpeg.org/) is not bundled with this project.

## Validation boundary

The earlier Windows baseline passed 9 CTest entries, 96 offline scenarios and
the actual VST3 factory/bus/state checks. Track checks include multitrack and
mixed-channel MIDI, tempo/SMPTE, controllers, latest selection, project restore,
disabled chord selection, retained full mix, stem protocol, timeout/resume,
duplicate protection, response expansion/deduplication and cache reuse.

One authorized separation of an existing song was tested; later operations reused
its parent task. The keys/piano stem passed HTTPS download, M4A/Opus decoding,
187.573-second rendering and UI selection checks (peak 0.791286, RMS 0.127243).
All 12 stems were not individually downloaded and auditioned. Offline rendering
is not evidence of native mouse dragging in Live.

