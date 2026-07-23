# Gloopy — Product Requirements Document

**A pattern-based, FL Studio–inspired DAW built in C++ with JUCE.**

- **Status:** Draft v1
- **Date:** 2026-07-22
- **Owner:** anthony@atgreen.org
- **Supersedes:** the initial single-timeline piano-roll MVP (M0, shipped)

---

## 1. Vision

Gloopy is a music-production application whose workflow mirrors **FL Studio
(FruityLoops)**: you build short **patterns** on a **step sequencer**, layer
melodies in a **piano roll**, arrange those patterns into a full song on a
**playlist**, and shape the sound through a **mixer** with insert effects.

The defining experience is *fast, loop-driven beat-making that scales up to a
finished arrangement* — not the linear tape-style timeline of a traditional DAW.

**North-star "hello world":** open Gloopy, program a drum beat and a bassline
across a couple of patterns, paint them into the playlist as intro / verse /
chorus, press play, and hear a complete arranged song — using only sounds that
ship with the app.

---

## 2. Goals & Non-Goals

### Goals (v1)
- The four FL pillars: **Channel Rack + Step Sequencer**, **Playlist**,
  **per-channel Piano Roll**, and a **Mixer with insert effects**.
- Channels are **either a sample player or the built-in synth**.
- A **bundled drum kit** (procedurally generated kick/snare/hat/clap) so the app
  makes sound with zero downloads.
- **Save / load** projects to disk.
- Reuse the existing engine (synth, transport, audio-thread plumbing) rather
  than rewrite it.

### Non-Goals (v1 — explicitly deferred)
- **VST3 / AU / CLAP plugin hosting** (instruments or effects).
- Audio-clip recording or audio clips on the playlist (pattern clips only in v1).
- Automation clips / parameter automation.
- MIDI hardware I/O, MIDI export, audio (WAV/MP3) export/render.
- Time-stretching, warping, advanced sampler (slicing, granular).
- Sends/returns busing beyond a single master sum (basic sends are a stretch goal).

---

## 3. Target User & Use Cases

- **Primary:** a hobbyist/producer who thinks in loops and patterns and wants a
  fast beat sketchpad that can grow into a song.
- **Secondary (and the actual driver here):** the developer building it — a
  clean, hackable C++/JUCE codebase that demonstrates real DAW architecture.

**Key use cases**
1. Program a 16-step drum groove and loop it. *(Channel Rack)*
2. Add a synth channel and draw a bassline in the piano roll. *(Piano Roll)*
3. Create pattern variations (intro / verse / chorus). *(Patterns)*
4. Arrange those patterns into a full song. *(Playlist)*
5. Balance levels and add reverb/delay on a bus. *(Mixer)*
6. Save the project and reopen it later. *(Persistence)*

---

## 4. Domain Model

The data model is the spine of the app. Gloopy adopts FL's separation of
**what a sound is** (Channel/Generator) from **when it plays** (Pattern notes)
from **how the song is arranged** (Playlist) from **how it's mixed** (Mixer).

```
Project
├── tempo (BPM), time signature, ppq
├── ChannelRack
│   └── Channel[]              // one row per sound source
│         ├── name, color
│         ├── Generator        // Sampler | Synth  (polymorphic)
│         ├── defaultPitch     // note used when a step is toggled (e.g. C5)
│         ├── volume, pan
│         └── mixerTrack        // which mixer insert it routes to
├── Pattern[]                  // named loops
│     ├── name, color, lengthInSteps (default 16)
│     └── ChannelContent[channelId] -> Note[]   // notes for that channel in this pattern
├── Playlist
│     └── PlaylistClip[]        // { patternId, playlistTrack, startBeat, lengthBeats }
└── Mixer
      ├── MixerTrack[0] = Master
      └── MixerTrack[1..N]      // { volume, pan, mute, solo, Effect[] chain }
```

### 4.1 Notes, steps, and the "steps are notes" principle
A `Note` is `{ pitch, startBeat, lengthBeats, velocity }` (already defined in
`Source/Note.h`). **The step sequencer is a constrained editor over a channel's
note list**, exactly like FL:

- Toggling **step _i_ ON** inserts a `Note` at `startBeat = i * (patternLen/steps)`,
  `pitch = channel.defaultPitch`, fixed short length, default velocity.
- Toggling it **OFF** removes that note.
- Opening the **piano roll** for the same channel/pattern edits the *same* note
  list freely (any pitch, any length). A step shows as "on" when a note starts
  within that step's cell.

This unifies the two editors and means the sequencer, piano roll, and playback
all read one representation.

### 4.2 Generators
Both generators are driven by note events (`pitch`, `velocity`, on/off) so the
scheduler treats them uniformly.

| Generator | Behaviour | Key params |
|-----------|-----------|------------|
| **Sampler** | Plays a loaded `.wav` as a one-shot per trigger; note number sets playback rate relative to a root note. | sample file, root note, gain, (later: start/end, loop) |
| **Synth** | The existing 16-voice oscillator + ADSR. | waveform, ADSR, gain |

### 4.3 Playback modes
A transport switch selects, à la FL:
- **Pattern mode** — loop the currently selected pattern (the beat-making view).
- **Song mode** — play the playlist arrangement start to end.

---

## 5. Feature Requirements

### 5.1 Channel Rack + Step Sequencer *(pillar 1)*
- Vertical list of channels; each row shows name, a mute toggle, and a **16-step
  grid** for the selected pattern.
- Click a step to toggle; steps show velocity via brightness (stretch: per-step
  velocity drag).
- Add / remove / rename / reorder channels; pick channel type (Sampler or Synth)
  on add. Drag a `.wav` onto the rack to create a Sampler channel.
- Per-channel volume & pan; solo/mute.
- Configurable steps-per-bar (16 default; 8/32 optional) and pattern length.

### 5.2 Sound sources *(Sampler + Synth)*
- **Sampler:** load `.wav` (16/24-bit, mono/stereo, common rates → resampled to
  device rate on load); one-shot playback; pitch by note. Voice pool per channel
  so repeated triggers don't cut abruptly (configurable choke later).
- **Synth:** reuse `SynthEngine` / `SynthVoice` as a per-channel generator.
- **Bundled kit:** procedurally synthesized **kick, snare, closed hat, clap**
  written to `assets/kit/*.wav` (see §8). Default new project loads these into
  four Sampler channels + one Synth channel.

### 5.3 Piano Roll *(pillar 2)*
- Reuse and generalize the existing `PianoRoll` component to edit **one channel's
  notes within one pattern**.
- Launch from a channel row ("piano roll" button) or a dedicated view.
- All current interactions carry over (add/move/resize/delete, snap, playhead).
- Length follows the pattern length; horizontal scroll/zoom for longer patterns.

### 5.4 Playlist *(pillar 3)*
- Multi-track timeline; rows hold **pattern clips**.
- Paint a clip by click-drag; move/resize/delete clips; snap to bar/beat.
- Clip shows its pattern's name/color.
- **Song mode** plays all clips; the playhead sweeps the arrangement and loops
  the song (or a selected loop region).
- Bars/beats ruler; configurable song length (grows as clips are added).

### 5.5 Mixer + insert effects *(pillar 4)*
- **Master** track + N insert tracks; each channel routes to one insert.
- Per-track: volume fader, pan, mute, solo, peak meter.
- **Effect chain** per insert (ordered list). v1 built-in effects:
  - **Gain/Trim**, **Filter/EQ** (low/high/band), **Delay**, **Reverb**.
  - Effects expose a small param set; UI is a simple generic panel per effect.
- Signal flow v1: `channel → insert chain → master chain → output` (linear).
  Sends/returns are a stretch goal.

### 5.6 Transport & clock
- Play / pause / stop, tempo, Pattern↔Song toggle, loop.
- Metronome (optional), count-in (stretch).
- Extends the existing `Transport`.

### 5.7 Project persistence
- Save/load a project file (`.gloopy`) capturing the full model.
- Format: JUCE `ValueTree` → XML (or JSON). Sample references stored by path
  (v1); embedded/consolidated assets is a stretch goal.
- New / Open / Save / Save As; recent files.

---

## 6. UX / Screen Layout

FL-style multi-panel workspace. v1 target layout:

```
┌───────────────────────────────────────────────────────────────────────┐
│ Toolbar: [▶︎][⏸][⏹]  BPM 128  [PAT|SONG]  bars:beats   [Save][Open]      │
├───────────────┬───────────────────────────────────────────────────────┤
│ CHANNEL RACK  │  PLAYLIST                                               │
│ ┌───────────┐ │  Pat1 [████][████][    ][████]                          │
│ │Kick  ■···■│ │  Pat2 [    ][████][████][████]                          │
│ │Snare ··■··│ │  Pat3 [        ][████████]                              │
│ │Hat   ■■■■■│ │                                                         │
│ │Bass ♪(pno)│ │  (song-mode playhead sweeps →)                          │
│ └───────────┘ ├───────────────────────────────────────────────────────┤
│  [+ channel]  │  PIANO ROLL (selected channel × pattern)               │
│               │   C5 ▓▓__▓▓__                                           │
├───────────────┴───────────────────────────────────────────────────────┤
│ MIXER:  [Master]│[Ins1 Kick]│[Ins2 Snare]│[Ins3 Bass]  faders + FX      │
└───────────────────────────────────────────────────────────────────────┘
```

Panels can start as fixed regions; dockable/resizable panels are a later polish.
Pattern selection and the Pat/Song toggle drive what the piano roll and playhead
show.

---

## 7. Technical Architecture

### 7.1 Reuse from M0 (already built)
- `Transport` (extend with play mode + song position).
- `SynthEngine` / `SynthVoice` (becomes the Synth generator).
- `Sequencer`'s note→MIDI-per-block logic (generalized to per-channel, per-pattern).
- `PianoRoll` (generalized to a channel/pattern editor).
- CMake + JUCE 8.0.15 fetch, audio-thread patterns (atomics + try-lock).

### 7.2 New core: `AudioEngine`
Owns the device callback and, per block:
1. Reads transport (mode, tempo, position).
2. Builds a per-channel MIDI event list for the block:
   - **Pattern mode:** from the active pattern's notes (looped).
   - **Song mode:** from every playlist clip overlapping the play range,
     offset to the clip's start.
3. Renders each channel's generator into its **mixer-insert buffer**.
4. Processes each insert's effect chain; sums inserts → master chain → output.

### 7.3 Threading model
- **Continuous params** (volume, pan, ADSR, effect knobs, tempo): `std::atomic`,
  written by the message thread, read by audio — as today.
- **Note/step edits:** per-pattern note lists guarded by a `CriticalSection`;
  audio thread uses a **try-lock** and falls back to the last state (as in the
  current `Sequencer`).
- **Structural edits** (add/remove channel, add/remove effect, load sample):
  performed on the message thread; audio thread sees them via a swapped
  immutable snapshot or under a short structural lock. A **command/snapshot**
  approach (build new structure off-thread, atomically publish a pointer) is the
  target to keep the audio thread allocation- and lock-free for structural work.
- No allocation, locking (beyond try-lock), or file I/O on the audio thread.

### 7.4 Suggested module layout (grows the current `Source/`)
```
Source/
  app/        Main.cpp, MainComponent (workspace shell)
  model/      Project, Channel, Pattern, PlaylistClip, MixerTrack, Note
  engine/     AudioEngine, Transport, Scheduler
  generators/ Generator (base), Sampler, Synth (wraps SynthEngine)
  fx/         Effect (base), Gain, Filter, Delay, Reverb
  ui/         ChannelRackView, StepSequencerView, PianoRoll,
              PlaylistView, MixerView, Toolbar
  assets/     kit generator + generated .wav
```

---

## 8. Bundled Drum Kit

A tiny offline synthesizer produces four one-shots so the app is self-contained:

| Sound | Recipe (approx.) |
|-------|------------------|
| **Kick** | Sine with fast downward pitch sweep (~150→50 Hz) + short amp click, exp decay. |
| **Snare** | Filtered white noise + a ~180 Hz body tone, medium decay. |
| **Closed hat** | High-passed white noise, very short decay. |
| **Clap** | Several short noise bursts (3–4) with tiny delays + a reverb-ish tail. |

Delivery options (decide at build time): (a) generate `.wav` files into
`assets/kit/` via a small standalone generator run once, or (b) synthesize into
memory at first run. Either way they are committed/available so a fresh clone
just works.

---

## 9. Milestones (phased delivery)

Each milestone is independently runnable and demoable.

> **Status (2026-07):** M0–M5 all implemented. The v1 scope is complete;
> remaining items live in §11 (future work).

| # | Milestone | Delivers | Exit criteria |
|---|-----------|----------|---------------|
| **M0** | *(done)* Synth + piano roll + transport | Foundation | Shipped |
| **M1** | Channel Rack + Step Sequencer + Sampler + bundled kit | **Make a drum beat** | Multiple sample/synth channels; 16-step grid; one pattern loops with kit sounds |
| **M2** | Patterns + per-channel Piano Roll | Melodies & variations | Multiple patterns; switch patterns; piano roll edits a channel's notes; pattern-mode playback |
| **M3** | Playlist + Song mode | **The north-star: a full arranged song** | Paint pattern clips; Pat/Song toggle; song plays end-to-end |
| **M4** | Mixer + insert effects | Mix & polish | Channels route to inserts; faders/pan/meters; gain/filter/delay/reverb; master sum |
| **M5** | Save / load + QoL | Persistence & polish | Project round-trips to disk; new/open/save; recent files |

**Recommended first build:** M1 — it establishes the Channel Rack, the
generalized generator interface (Sampler + Synth), and the bundled kit, which
everything else builds on. M3 is where the stated hello-world goal is met.

---

## 10. Non-Functional Requirements
- **Latency:** glitch-free at 256–512 sample buffers @ 44.1/48 kHz on a typical desktop.
- **Audio-thread safety:** no locks (beyond try-lock), allocation, or I/O in the callback.
- **Platforms:** Linux first (dev target); keep code portable to macOS/Windows (JUCE gives this largely for free).
- **Build:** single `cmake --build`; JUCE fetched automatically; no manual SDKs for v1 (no VST).
- **Code quality:** modular model/engine/ui separation; components independently testable.

---

## 11. Open Questions / Future
- **Sends/returns** in the mixer (v1 is linear-to-master) — promote to v1?
- **Per-step parameters** (velocity, pitch, roll/ratchet) — how much of FL's
  step "graph editor" to bring in.
- **Audio clips & recording** on the playlist (post-v1).
- **Plugin hosting** (VST3/CLAP) — the largest future expansion.
- **Export/render** to WAV — likely the first thing wanted after M3.
- Kit delivery: generate-to-file vs. synthesize-at-runtime (§8).

---

## Appendix A — Glossary (FL terms → Gloopy)
- **Channel Rack** → `ChannelRack` / `ChannelRackView`
- **Step / step sequencer** → constrained editor over a channel's `Note[]` in a pattern
- **Pattern** → `Pattern` (named per-channel note sets)
- **Playlist** → `Playlist` of `PlaylistClip`s (pattern clips in v1)
- **Generator / Channel plugin** → `Generator` (`Sampler` | `Synth`)
- **Insert / FX track** → `MixerTrack` with an `Effect[]` chain
- **Pattern vs. Song mode** → `Transport` play-mode toggle
