# Gloopy roadmap

Implementation plans for the next round of features, ordered by leverage. Each
entry has a **goal**, an **approach** grounded in the current code, concrete
**steps**, and an effort estimate (**S** ≈ hours, **M** ≈ a day or two, **L** ≈ a
week+). Pick top-down; later phases assume the earlier plumbing exists.

## Architecture constraints that shape these plans

- The **runtime model is plain C++** (`std::vector<std::unique_ptr<Track>>`,
  `MixerTrack`s, atomics) — `ValueTree` is used *only* for save/load
  (`toValueTree`/`loadFromTree`). So undo can't just wrap a live `ValueTree`.
- Three threads: **audio** (`renderBlock`, lock-free, `engineLock` try-lock),
  **message** (GUI + structural edits under `engineLock`), and the **control API**
  (OSC/gRPC). New realtime data must reach the audio thread lock-free or under the
  try-lock; new structural ops go on the message thread (`callOnMessageThread`).
- Clips are scheduled to MIDI each block by `collectNotes` (`NoteScheduler.h`);
  live notes arrive via each track's `juce::MidiMessageCollector liveMidi`.

---

## Phase 1 — Core gaps & quick wins

### 1.1 MIDI recording into clips  — **M**
**Goal:** arm a track, hit Record, play the MIDI keyboard (or OSC), and capture the
performance into a new MIDI clip.
**Approach:** the capture path already exists — `handleIncomingMidiMessage` routes
to the selected track's `liveMidi`, drained in `renderBlock`. Add a record-enabled
state and a lock-free timestamped capture buffer the audio thread appends to.
**Steps:**
- `recordButton.onClick` → toggle `transport` "recording" + start playback; add a
  `std::atomic<bool> recording` and a per-armed-track lock-free FIFO of
  `(songSampleTime, MidiMessage)`.
- In `renderBlock`, when recording, copy the merged live-MIDI (already computed)
  into the FIFO tagged with the current playhead sample.
- On stop, drain the FIFO on the message thread, convert sample-times → beats,
  build a `Clip` (reuse `apiAddClip`), quantize-on-input optional.
- UI: red overlay on the armed track; a count-in (1 bar) before capture.
**Risks:** overdub vs replace semantics; keep it "replace into a new clip" first.

### 1.2 Master limiter + clip meters  — **S/M**
**Goal:** stop the constant 0 dBFS clipping we hit building demos; show when it happens.
**Approach:** add a `Limiter` effect (`EffectFactory` in `Effects.h`) — a simple
lookahead brickwall (JUCE `dsp::Limiter` or a hand-rolled one-pole). Auto-insert on
the master, and track a per-insert `clipped` atomic.
**Steps:**
- New `Effect` subclass wrapping `juce::dsp::Limiter`; register in `EffectFactory`
  and the `EffectType` proto enum.
- Set `clipped` on any `MixerTrack` whose block magnitude ≥ 1.0; expose in the
  meters (`Meters` proto already streams per-insert peaks — add a `clipped` flag).
- `MixerView`: red clip LED per channel, click-to-reset.
**Risks:** don't limit by default in a way that surprises; make it a normal insert
that the default project happens to add on master.

### 1.3 Fill the control-API mixer gap  — **S**
**Goal:** the API can't set mixer-insert volume/pan/mute (only *track* params). Needed
for automation targets and full remote control.
**Steps:** add `SetInsertParams` RPC + `apiSetInsertParams` (mirrors
`apiSetTrackParams`, writes the `MixerTrack` atomics under `engineLock`). Add to the
OSC lane too (`/gloopy/insert/<n>/vol|pan|mute`).

### 1.4 MIDI hot-plug  — **S**
**Goal:** keyboards plugged in after launch are seen without a restart.
**Approach:** `deviceManager.addChangeListener(this)`; on change, diff
`MidiInput::getAvailableDevices()` against `openMidiInputs` and open the new ones
(reuse `setupMidiInputs`). *(The built-in synth already hard-stops voices via
`allNotesOff(1,false)`, so the plugin Stop fix leaves stop behavior consistent.)*

---

## Phase 2 — Grown-up DAW

### 2.1 Undo / redo  — **M/L**
**Goal:** every structural edit is undoable.
**Approach:** the runtime model is plain C++, so the cheapest robust route is
**snapshot-based**: `toValueTree()` already serializes the whole project.
Keep a bounded stack of `ValueTree` snapshots; undo = `loadFromTree(previous)`.
Coalesce rapid edits (drags) by snapshotting on gesture-end.
**Steps:**
- Wrap every mutation entry point (`addTrack`, `apiAddClip`, effect add/remove,
  clip move, param edits committed from the UI) with a `pushUndoSnapshot()` before
  the change.
- `Ctrl+Z` / `Ctrl+Shift+Z` → `loadFromTree` of the adjacent snapshot; refresh UI.
- Exclude audio-sample blobs from the snapshot diff to keep it cheap (store audio
  clips by reference/hash, not re-serialized each time).
**Risks:** snapshot cost with embedded audio — hence the reference trick; or move to
a proper command pattern later if snapshots get heavy.

### 2.2 Parameter automation  — **L**
**Goal:** draw volume/pan/effect-param/plugin-param curves over the timeline.
**Approach:** an automation lane owns breakpoint envelopes keyed by a target id
(track vol, insert param, effect param, plugin parameter index). The audio thread
evaluates each active envelope at the block's playhead and writes the target atomic
before rendering.
**Steps:**
- Model: `struct AutomationLane { TargetRef target; std::vector<Point{beat,value}>; }`
  hung off a `Track` or the project; serialize in `toValueTree`.
- Audio: in `renderBlock`, before track/insert processing, sample each lane at the
  current beat (linear interp) and store into the target atomic / call the plugin's
  `setParameter`. Reuse the `TargetRef` scheme from the control API.
- UI: an automation sub-lane in the arranger with draggable breakpoints; a
  "write" mode that records incoming OSC/param changes as points (killer combo).
- API: `SetAutomation`/`GetAutomation` RPCs so it's scriptable.
**Risks:** plugin-parameter automation timing/threading; start with built-in
targets (vol/pan/effect params) then add plugin params.

### 2.3 In-app SFZ / sample loading  — **M**
**Goal:** remove the state-injection hack — let users load an SFZ into a sampler
track from a file chooser.
**Approach:** two options, do the first now: (a) a **native SFZ opcode subset** in a
new `Generator` (region → sample map, ampeg, key/vel ranges) — reuses the `Sampler`
voice code and makes projects portable; (b) keep hosting sfizz but add a
"set SFZ file" action that writes the retargeted plugin state (port
`tools/sfizz-state.py` logic into C++). (a) is more work but removes the external
dependency and the whole reverse-engineered-state fragility.
**Steps (a):** parser for `<region>`/`<group>` + the handful of opcodes VPO uses
(`sample`, `lokey/hikey/pitch_keycenter`, `lovel/hivel`, `ampeg_*`, `loop_mode`);
disk-streaming voice (already have preload logic patterns); file chooser on the
sampler track.
**Risks:** SFZ is a big spec — scope to the opcodes the bundled libraries actually use.

---

## Phase 3 — Lean into the differentiator (the control API)

### 3.1 Structural change events on `Subscribe`  — **S/M**
**Goal:** external clients (and future multi-client editing) stay in sync; today
`Subscribe` only streams transport + meters.
**Steps:** add `Event` oneof variants (`track_added/removed`, `clip_changed`,
`effect_changed`, `param_changed`). Emit from the message-thread mutation points
(same hooks as undo) into a lock-free queue the `Subscribe` loop drains. Bump the
proto and the CL/So clients.

### 3.2 RenderRegion / stem export  — **S/M**
**Goal:** bounce a beat-range or a single track/insert, not just the whole song.
**Approach:** generalize `apiRenderToFile` — it already pumps `renderBlock` offline.
Add `start_beat`/`end_beat` and an optional `track_id`/`insert` (solo that route).
**Steps:** `RenderRequest` gains range + target fields; the offline loop seeks to
`start_beat`, mutes non-target routes, stops at `end_beat` + tail.

### 3.3 Live-coding console + Python client  — **M**
**Goal:** make "DAW as a REPL" a first-class story.
**Steps:** ship a Python gRPC client mirroring `examples/gloopy-grpc.lisp` (generate
from the proto, thin plist/dict wrappers); add a small in-app text console that
evals against the control API (or just document the external-REPL workflow well).
Great for generative music, teaching, and demos.

---

## Phase 4 — Sound & quality

### 4.1 Better built-in synth (or a preset pack)  — **M**
**Goal:** the demos leaned on plugins because `SynthEngine` is one osc + ADSR.
**Approach:** extend `SynthVoice`/`SynthEngine` to 2 oscillators (detune/mix), a
resonant filter with its own envelope, and one LFO (→ pitch/filter/amp). Serialize
the new params; expose via `AddSynthTrack`/OSC. Alternatively ship a curated set of
named presets over the existing engine as a fast win.

### 4.2 Groove/swing + piano-roll velocity editing  — **S/M**
**Goal:** humanize and shape dynamics.
**Steps:** a per-clip/track swing amount applied in `NoteScheduler` (shift
off-beats); optional per-note timing/velocity randomization. In `PianoRoll`, a
velocity strip under the notes with drag editing.

### 4.3 Tests + CI  — **M**
**Goal:** catch regressions like the VST3 reload-path bug before they ship.
**Steps:** a small unit target (JUCE `UnitTest` or Catch2) for pure logic —
`NoteScheduler` collection, save→load round-trip of `toValueTree`, the sfizz-state
codec, proto encode/decode; a headless smoke test that boots Gloopy, drives a few
gRPC calls, and renders. Wire into CI (`cmake --build` + run tests).

---

## Suggested order
1. **1.1 MIDI recording** and **1.2 limiter/meters** (immediate feel).
2. **1.3 insert API** + **1.4 hot-plug** (small, unblock later work).
3. **2.1 undo/redo** then **2.2 automation** (the "grown-up DAW" tier).
4. **3.x control-API** features in parallel with Phase 2 (they share the mutation
   hooks from undo).
5. **2.3 in-app SFZ** and **Phase 4** as sound/quality investments.
