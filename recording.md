# Recording Plan

> **Status: Phases 1–2 implemented.** `Source/Recording.cpp` captures audio input to a
> WAV take and drops a *referencing* audio clip (no embedded blob). The audio
> device now opens inputs (`setAudioChannels(2, 2)`); the audio thread copies input
> into a JUCE `ThreadedWriter` (bounded FIFO + background writer thread), and the
> message thread opens/closes writers and creates clips. Takes land in a
> composition's `assets/recordings/`, with metadata appended to `recordings/takes.toml`.
> Clips reference the take by path + `take` id — `Clip.audioFile` is serialised as a
> reference in both `.gloopy` and the composition format (no duplication; the take is
> not pruned). Control: `ListAudioInputs`, `ArmTrack` RPCs, and the existing
> `StartRecording`/`StopRecording` now also drive armed **audio** tracks (one record
> trigger captures MIDI on the MIDI-armed track and audio on armed audio tracks).
> A red **●** arm button appears on audio-track headers.
>
> Verified end-to-end (via a `GLOOPY_REC_TEST_TONE_HZ` self-test seam that injects a
> tone in place of the mic, since CI has no microphone): a 440 Hz take records to a
> valid 24-bit WAV, the clip plays back at 440 Hz, and it survives a composition
> save→reload as a reference.
>
> **Phase 2** adds: **multiple armed tracks** (one record trigger opens a take
> writer per armed audio track); per-track **dry monitoring** (`recordMonitor`,
> input routed to output through track volume while armed); **count-in** (rewind
> playback before the anchor, start writing at the anchor) and **punch range**
> (write only within `[in, out)`), via `SetPunchRange`; and **recording events**
> on `Subscribe` (`recording_started` / `take_created` / `recording_stopped` /
> `recording_error`, on the change stream). `ArmRequest` gained a `monitor` flag.
> Verified with the tone seam: N armed tracks yield N takes/clips/events; a
> punch `[2,4)` with count-in 1 records exactly 2 beats anchored at beat 2.
>
> Deferred to Phase 3: latency-compensation UI, failed/partial take recovery,
> take promotion/cleanup, FLAC, loop recording and take lanes. Monitoring routes
> dry input only (insert effects not printed).

## Goal

Add first-class audio recording to Gloopy without returning to embedded audio
blobs. Recording should create ordinary audio files, place clips on the
arrangement, and keep the editable project state in human-readable text.

The core workflow:

1. Select or create an audio track.
2. Choose an input device/channel pair.
3. Arm the track.
4. Press record.
5. Gloopy writes a take file under the composition directory and creates an
   audio clip that references it.

## Scope

Initial scope:

- mono and stereo audio input recording;
- one or more armed audio tracks;
- direct monitoring on/off per armed track;
- record-to-arrangement from the current playhead;
- optional count-in;
- punch-in/punch-out range;
- take files saved as WAV;
- clip references to recorded files;
- take metadata saved as text.

Deferred:

- comp lanes;
- take folders with swipe comping;
- input effects printed into recordings;
- latency calibration UI;
- overdub/replace modes for MIDI and audio together;
- time-stretching recorded clips to tempo changes.

## Files On Disk

In a composition directory, recorded audio should live under `assets/recordings/`
by default:

```text
my-song/
  gloopy.toml
  tracks/
    vocal.toml
  clips/
    vocal/
      verse-1.audio.toml
  recordings/
    takes.toml
  assets/
    recordings/
      vocal-main-001.wav
      vocal-main-002.wav
```

The WAV files are binary assets. They are not embedded in TOML or XML.

`recordings/takes.toml` stores take metadata:

```toml
[[takes]]
id = "vocal-main-001"
file = "../assets/recordings/vocal-main-001.wav"
track = "vocal-main"
input = "system:capture_1"
channels = 1
sample_rate = 48000
start_beat = 32.0
length_beats = 16.0
created = "2026-07-24T18:42:10-04:00"
```

Audio clips reference take files:

```toml
id = "vocal-verse-1"
type = "audio"
take = "vocal-main-001"
file = "../assets/recordings/vocal-main-001.wav"
start = 32.0
offset_seconds = 0.0
length_beats = 16.0
gain = 1.0
fade_in = 0.0
fade_out = 0.0
```

The `take` ID is the stable project identity. The `file` path is included so the
clip remains readable and recoverable if take metadata is missing.

## Track State

Audio input state belongs on audio tracks:

```toml
[recording]
armed = false
input = "system:capture_1"
channels = 1
monitor = false
record_folder = "../assets/recordings"
```

These fields are project state, not global preferences. The selected hardware
device may not exist on another machine, so missing inputs should produce a
diagnostic rather than preventing the project from loading.

## Runtime Design

JUCE already owns audio device setup through `AudioAppComponent`. Recording adds
an input capture path to the audio callback:

1. At record start, the message thread snapshots armed tracks and creates a take
   writer for each one.
2. The audio thread copies input channel samples into per-track recording FIFOs
   or directly into pre-opened audio writers if that path is proven safe.
3. A background writer thread drains FIFOs and writes WAV data.
4. At record stop, the message thread closes each writer, creates take metadata,
   and adds audio clips to the target tracks.

Do not allocate, open files, close files, or block on disk from the audio thread.
The audio thread should only copy samples into bounded buffers and advance
recording counters.

## Monitoring

Monitoring should be explicit per track:

- `monitor = false`: record input but do not route live input to the track output.
- `monitor = true`: route input through the track's mixer insert while recording
  or while the track is armed.

Initial monitoring can be dry input routed through the track volume/pan and
mixer insert. Printing monitored insert effects into the recorded file is
deferred; recordings should initially capture dry input.

Feedback prevention matters. If the selected input is also receiving Gloopy's
speaker output through the system mixer, Gloopy cannot reliably detect that.
The UI should make monitoring state visible and easy to turn off.

## Punch And Count-In

Count-in:

- when enabled, playback starts before the recording point;
- take writing starts only at the record start beat;
- the created clip starts at the record start beat.

Punch range:

- punch-in and punch-out are beat positions;
- outside the punch range, armed tracks monitor according to their monitor flag
  but do not write samples;
- at punch-out, Gloopy stops writing and closes the clip length while playback
  may continue.

Loop recording is deferred. The first version should stop writing at punch-out
or transport stop and create one take per armed track.

## Latency

Recorded clips need latency compensation:

- use JUCE device latency values where available;
- store the applied compensation in take metadata;
- allow a global manual offset preference later.

Initial implementation can store `latency_compensation_seconds = 0.0` and leave
manual calibration as a follow-up, but the file format should have a place for
it:

```toml
latency_compensation_seconds = 0.0
```

## Control API

Add gRPC commands for structural recording control:

- `ListAudioInputs`
- `SetTrackInput`
- `ArmTrack`
- `StartRecording`
- `StopRecording`
- `SetPunchRange`

OSC remains the live performance lane and should not carry audio sample data.

Recording events should appear on `Subscribe`:

- `recording_started`
- `recording_stopped`
- `take_created`
- `recording_error`

## UI

Minimum UI:

- input selector on audio tracks;
- arm button;
- monitor button;
- record button in the transport;
- visible recording status and elapsed time;
- clear error state for missing input, disk write failure, or buffer overrun.

The arranger should create and select the new audio clip after recording stops.

## Failure Handling

Important failures:

- input device missing;
- requested input channel missing;
- output directory not writable;
- disk full;
- writer thread cannot keep up;
- user stops recording before any samples are written.

On failure, Gloopy should close partial files, keep recoverable files when they
contain audio, and create diagnostics. It should not create an empty clip for an
empty or failed take.

## Git Policy

Recorded audio is ordinary binary data. The generated composition `.gitignore`
should make room for both workflows:

```gitignore
.gloopy-cache/
exports/
assets/recordings/raw/
*.wav.tmp
```

By default, Gloopy can record into `assets/recordings/raw/` for scratch takes and
offer "promote take" to move a keeper into `assets/recordings/`. Projects that
want every take versioned can remove that ignore rule.

## Implementation Phases

Phase 1:

- add audio input selection and track arming;
- record one stereo or mono take to WAV;
- create one audio clip on stop;
- save take metadata in `recordings/takes.toml`;
- render/play the recorded clip through the existing audio clip path.

Phase 2:

- support multiple armed tracks;
- add monitoring;
- add count-in and punch range;
- add gRPC recording controls and subscription events.

Phase 3:

- latency compensation UI;
- failed/partial take recovery;
- take promotion and cleanup commands;
- optional FLAC recording;
- loop recording and take lanes.
