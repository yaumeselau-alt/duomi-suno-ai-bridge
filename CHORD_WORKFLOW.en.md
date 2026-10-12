# Editable piano block chords — English

[简体中文](CHORD_WORKFLOW.md) | **English**

The plugin keeps original MIDI and automatically creates a separate piano block
chord file. Analysis is local and makes no paid API request.

## In the plugin

1. Generate MIDI or click **Open MIDI**. Set BPM close to the material's actual tempo.
2. Local analysis reports merged chord segments, segments needing review and
   skipped analysis windows.
3. Switch from **MIDI preview** to **Block chords**.
4. Hold the piano roll and drag to a Live MIDI track with a piano instrument,
   or click **Export chord MIDI**.
5. Edit notes, inversions, register, velocity, rhythm or transposition in Live.
   Switch back to MIDI preview to access the original result.
6. Choose **Every beat / Every 2 beats / Every 4 beats** to adjust the analysis
   window. Adjacent identical chords merge, so output can span multiple windows.
   Changing BPM or the interval automatically reanalyses; **Rebuild piano block
   chords** explicitly retries.

Output is standard MIDI with 3 or 4 simultaneous notes per segment, sharing start
and end times. It excludes the original melody and drum channel. The GM program
is piano; the DAW instrument determines the actual sound. The plugin still uses
its simple preview synthesizer.

## Analysis

Original MIDI → exclude channel 10 → windows at selected BPM and 1/2/4 beats →
aggregate pitch-class duration and velocity → score chord templates → use
neighbouring evidence and a whole-sequence path to reduce interruptions →
merge identical neighbours → generate independent simultaneous notes.

- Templates: major, minor, diminished, augmented, sus2, sus4, dominant seventh,
  major seventh, minor seventh and half-diminished seventh.
- Duration/velocity coverage and bass support score 120 root/type candidates.
  Complete, strongly supported candidates provide neighbouring evidence.
- A partial window retaining at least two chord tones can borrow a complete
  candidate from an immediately adjacent window. A single tone requires both
  neighbours to support the same chord. Borrowed segments need review.
  Silence is not filled, and long monophonic passages are not forced into chords.
- Dynamic programming mildly penalizes unsupported rapid changes while keeping
  clear root/type changes, such as C–C7–C.
- Consecutive identical root/type pairs become one sustained note group and marker.
  Rests, unanalyzable windows or changed chord types break the group.
- Output uses root position, a middle register and fixed velocity to provide an
  editable skeleton. Arpeggios become simultaneous notes.
- Weak evidence is skipped; ambiguous/noisy/borrowed results are marked for review.
  That marker is a heuristic, not a statistical confidence probability.
- Chord-name MIDI markers may be displayed by some DAWs; notes are always editable.
- Original MIDI is not overwritten. Successful files remain in the user
  application-data directory DuomiSunoAIBridge/ChordExports.

The idea of reducing polyphony to harmony is also described in
[music21's chordify guide](https://www.music21.org/music21docs/usersGuide/usersGuide_09_chordify.html).
This plugin implements native C++ fixed-beat template analysis, requires no Python
or music21, and does not reproduce chordify note-for-note.

## Limits and evidence

Editable block chords do not guarantee accurate recovery of the song's original
harmony. Extracted MIDI may omit accompaniment, contain wrong notes or be only
a melody; the original harmony is then underdetermined and needs human review.

Windows start at time zero. Automatic beat tracking, pickup detection, source
tempo-map following and complex time-signature inference are not implemented.
Export uses the selected BPM, 4/4 and root position. Free tempo, syncopated chord
changes or pickups may require manual boundary edits. Extended chords, recovered
inversions, optimized voice leading and in-plugin chord-name editing are future work.

An existing C Major Drift sample at 120 BPM, every 2 beats changed from
172 segments / 592 notes / 14 skipped windows to 119 segments / 413 notes /
7 skipped windows, with 54 merged segments flagged for review. Fewer segments
include merged identical neighbours; fewer is not necessarily more accurate.
File validation and full rendering passed; every chord has not been auditioned.

## Bar rulers

Piano roll and waveform share a BPM-based 4/4 grid starting at 1.1 (bar 1, beat 1).
The piano roll shows four bars per page; playback follows whole pages.
Thin lines indicate beats and strong lines bars. Full-song waveforms reduce label
density to available width, e.g. 1.1, 17.1, 33.1.

This is a reference grid. It does not quantize original notes, snap waveform
seeking, time-stretch audio or change Live's BPM. Source/host tempo and signature
changes are not followed. Transport also displays elapsed/total time.

## Audio-only material

A possible future path is instrument separation → polyphonic audio-to-MIDI →
block-chord analysis. [Spotify Basic Pitch](https://github.com/spotify/basic-pitch)
is an example transcription tool, not a chord recognizer. Duomi-generated audio
separation is implemented; local mixed-audio upload and the automatic
separation-to-transcription pipeline are not.

Prompts such as “piano accompaniment, clear chord progression, sustained block
chords, no arpeggios” may help, but cannot guarantee cloud output or replace analysis.

## Project restore and track selection

Host state saves the prompt, BPM, interval, mode, language, task IDs and original
MIDI bytes. Reopening restores MIDI and rebuilds chords in the background,
stopped at the start, without automatic playback or network requests.

Audio is saved as local WAV paths, not embedded. Missing files must be reassociated
with Open audio. Transfer WAV files when moving computers; Live's collected audio
does not automatically repair plugin-internal absolute paths. Keys are never
stored in project state; Windows uses separate DPAPI storage.

MIDI/audio modes can select existing tracks/stems. Block chords have one fixed
piano track and use the complete original MIDI as evidence. See
[Tracks and stems](TRACK_WORKFLOW.en.md) for cloud charges, local-audio limits
and FFmpeg requirements.

