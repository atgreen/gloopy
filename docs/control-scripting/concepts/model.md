# The Gloopy model

*This is the conceptual core of Gloopy. Every noun below is defined **here, once**.
The gRPC, OSC, Python, and Lisp references all link back to this page rather than
re-explaining what a clip or a scene is. If you add a term, add it here.*

Gloopy is a **linear arranger**: sound sources are laid out as tracks down a
timeline, and each track owns the clips placed along it. There is no separate
"pattern" or "channel rack" layer — the live model is simply **tracks and clips**.

## The nouns

### Composition (project)
The whole song: its tracks, mixer, transport settings, tempo map, locations,
automation, and metadata. "Composition" and "project" are the same thing.
A composition has two on-disk forms — a single `.gloopy` file or a
[composition-as-repo directory](#on-disk-two-forms).

### Track
A **sound source plus a row of clips on the timeline, plus its mix settings**.
Every track has a stable **`id`** (an integer), which is how the control API
refers to it — the `id` is printed to stdout when a track is created or a project
loads. There are three track **types**:

| Type | What it holds |
|------|---------------|
| **Instrument** | A [generator](#generator) (built-in synth, sampler, SFZ player, or a hosted plugin) played by MIDI notes. |
| **Audio** | Recorded or imported audio clips (`.wav`), drawn with waveforms. |
| **MIDI out** | Sends MIDI to an external device rather than making sound itself. |

!!! note "Synth, sampler, SFZ, and plugin are not track types"
    They are all **Instrument** tracks that differ only by their
    [generator](#generator). The distinction appears only when you *create* a
    track (`AddSynthTrack`, `AddSamplerTrack`, `AddSfzTrack`, `AddSurgeTrack`,
    `AddPluginTrack`), not in the model afterwards.

A track also carries: `volume`, `pan`, `mute`, `solo`, polarity, a target
[mixer track](#mixer-track-bus-send), a live-MIDI input collector, record-arm
state, an optional [arpeggiator](#arpeggiator), its `clips`, and its
[session slots](#scene-and-session-slot).

### Generator
The sound engine behind an Instrument track. One of: the built-in **Synth**
(waveform + ADSR), the **Sampler** (a `.wav` player with a start/end playback
window, reverse, per-voice fades, mono-choke voicing, and one-shot or **looping**
playback — a loop can **crossfade its seam** so it repeats without a click), the
vendored **sfizz** SFZ player, the embedded **Surge** core, or a **hosted plugin**
(VST3 / LV2). They
share one interface (prepare / render / all-notes-off), so the rest of the model
treats them uniformly.

### Clip
A block of content placed on a track's timeline. **A clip owns its content** —
notes or audio — rather than referencing a shared pattern. Two clip **types**:

- **MIDI clip** — holds [notes](#note). Its content has a length
  (`contentLenBeats`) and, by default, **loops** (`looped = true`): a 1-bar clip
  dropped onto a 4-bar span repeats four times. Also carries `transpose` and a
  velocity scale.
- **Audio clip** — references an audio buffer with a source sample-rate, gain,
  and fade-in/out. Audio is stored by reference, not embedded per clip.

Placement is in **beats**: `startBeat`, `lengthBeats` (so `endBeat = start +
length`). Clips can be split, duplicated, repeated, reversed, cropped, gained,
normalized, and bounced.

### Note
The atom of a MIDI clip: `pitch` (0–127), `startBeat`, `lengthBeats`, `velocity`
(0.0–1.0), and `probability` (0.0–1.0, for humanized/generative playback). Times
are **quarter-note beats** relative to the clip. The same note model backs both
the 16-step grid and the piano roll.

### Transport
The musical clock: **tempo (BPM)**, **swing** (0.5–0.9), **time signature**,
**loop region**, play state, and the **playhead** position in beats. One shared
transport drives the whole composition.

### Tempo map & time
Beyond the single BPM, a composition can hold a **tempo map** — a list of
`(beat, bpm)` markers — plus a time signature. Helpers convert between beats,
seconds, and bar·beat positions. The beat unit is always the quarter note.

### Scene and session slot
Gloopy has an Ableton-style **Session view** (a clip-launch grid) alongside the
linear arrangement:

- Columns are **tracks**; rows are global **scenes** (each scene has a name and
  colour).
- Each track holds one **session slot** per scene. A slot is either empty or
  contains a [clip](#clip) you launch live. The grid is always rectangular:
  every track has a slot for every scene.

Launching is quantized to a musical boundary; launching a clip on a track
overrides that track's arrangement playback until you return to the arrangement.
Session launching is a **live** feature — it is *not* on the offline-render path,
so renders remain deterministic and come from the arrangement.

The grid is scriptable: copy an arrangement clip into a slot, launch a clip or a
whole scene, stop a track or everything, set the **launch-quantize** window (the
musical boundary launches snap to — a project setting that persists), and read
back which slot each track is playing or has *queued* — all over the control API
(and `/gloopy/session/…` OSC for live triggers). A launch is queued and fires at
the next launch boundary, so the queried state shows the pending slot immediately
and the playing slot once it fires.

### Mixer track (bus, send)
A **mixer insert**: a fader, pan, mute/solo, peak meters, and an ordered chain of
[effects](#effect). **Mixer track index 0 is the master.** A mixer track can
route its `output` to the master (0) or to a **bus** (a higher-indexed mixer
track used as a submix), and can have **sends** — `(bus, level, post-fader)` —
that tap signal to other buses (e.g. a shared reverb).

Two lightweight grouping features sit on top:

- **Control group (VCA-lite)** — a group whose fader *scales* member volumes.
  It's control math, not extra audio routing.
- **Mixer scene** — a named snapshot of fader/pan/mute/solo/send/bypass values
  you can recall.

### Effect
A processor in a mixer track's chain. Gloopy ships a curated set of built-ins
(gain, filter, delay, reverb, limiter, bitcrusher, compressor, EQ, waveshaper,
stereo widener, tremolo, chorus, flanger, phaser, autopan, noise gate, autowah,
ring-mod) plus **hosted VST3 / LV2 effects**. Each effect exposes named
parameters. There are also non-mutating **analyzer** effects — a **scope**
(captures the passing waveform, drawn as an oscilloscope), a **spectrum** (an
octave-band real-time analyzer, drawn as level bars), and a **vectorscope**
(captures the stereo field, drawn as a goniometer). They pass audio through
untouched and expose a live snapshot you can read back over the API.

### Parameter (the universal parameter model)
Every automatable value — a track fader, a synth knob, an effect parameter — has
a **stable, string-addressed id** so automation, modulation, MIDI-learn, and the
control API all speak one language. The id grammar:

```
track/<id>/volume        track/<id>/pan       track/<id>/mute   track/<id>/solo
track/<id>/synth/<name>  insert/<index>/volume
effect/<insert>/<slot>/<paramName>
```

This is the spine the next four concepts hang off.

### Automation
Time-varying parameter values: **lanes** of `(beat, value)` points targeting a
[parameter](#parameter-the-universal-parameter-model), with step/curve shaping.
Automation plays back from the arrangement.

### Modulation
A **modulation route** connects an **LFO** (rate, depth, centre, shape,
tempo-sync, phase, unipolar/bipolar, slew) to a [parameter](#parameter-the-universal-parameter-model)
target — continuous movement without drawing points.

### Arpeggiator
A per-Instrument-track live arp (`ArpSpec`) that turns held/looped notes into
patterns in real time.

### Location
A named point or span on the timeline: a **marker**, **range**, **section**,
**loop**, **punch**, **export**, or **skip** region. Locations give render
profiles and punch-recording named targets like `chorus-2`.

### Export profile
A named render target — `mix`, `range`, `track`, or `stems` — so
"render the streaming master" or "bounce stems" is a saved, repeatable action.

### Take
A single recorded pass. Loop-recording stacks takes into lanes; takes can be
promoted, cleaned up, or recovered. Raw takes live under the composition's
`assets/recordings/`.

### Scale & tuning
A composition-level scale (for piano-roll highlighting and snap-to-scale) and a
tuning, including Scala `.scl` import.

### Controller map
A binding from a **source** — a MIDI CC (`cc:<n>`) or an OSC/API source
(`osc:<name>`) — to a [parameter](#parameter-the-universal-parameter-model)
target, the backbone of MIDI-learn. Scaling and bypass are per-mapping.

## The two control lanes

Gloopy is driven remotely over **two interfaces with different jobs**. This split
is the single most important thing to understand before scripting Gloopy — see
the [explanation](../explanation/index.md) for the full rationale.

| | **gRPC** | **OSC** |
|---|----------|---------|
| Job | **Structural & scheduled** — build and edit the composition | **Live & performative** — play it in real time |
| Examples | add track, add clip, set automation, render, save | note on/off, fader move, effect-knob tweak, play/stop |
| Identity | **Creates** the stable `id`s | **Acts on** ids gRPC created |
| Endpoint | TCP `127.0.0.1:50051` | UDP `9000`, addresses under `/gloopy` |
| Guarantees | request/response, full model | fire-and-forget, real-time safe |

The rule of thumb: **gRPC changes the score; OSC performs it.** They share
identity — you create a track over gRPC, learn its `id`, then play it over OSC.

## On disk: two forms

A composition serialises from the same in-memory model two ways:

1. **`.gloopy` file** — a single XML file with embedded sample and plugin-state
   blobs. Compact; good for interchange.
2. **Composition-as-repo directory** — a folder with a `gloopy.toml` manifest,
   human-readable TOML/text for tracks, clips (`.notes`), mixer, automation
   (`.points`), locations, scenes, and binary assets as sidecars under
   `assets/`. Designed for **readable diffs and version control** — a no-op
   re-save writes nothing. `gloopy pack` zips one up.

Both round-trip losslessly; the loader auto-detects file vs. directory vs. `.zip`.

---

*Related:* [gRPC reference](../reference/grpc/index.md) ·
[OSC reference](../reference/osc/index.md) ·
[Python client](../reference/python/index.md) ·
[Common Lisp client](../reference/lisp/index.md)
