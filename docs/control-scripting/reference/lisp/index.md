# Common Lisp client

The Lisp client is the ASDF system `gloopy`, in
[`common-lisp/`](https://github.com/atgreen/gloopy/tree/main/common-lisp)
(dependencies vendored via [ocicl](https://github.com/ocicl/ocicl)). It mirrors
the two [control lanes](../../concepts/model.md#the-two-control-lanes) as two
packages. For a guided start, do the
[Common Lisp quickstart](../../tutorials/lisp-quickstart.md).

!!! info "Hand-maintained (for now)"
    Unlike the gRPC / OSC / Python references, this page is **written by hand**
    from the export lists and `:documentation` strings in `common-lisp/src/`. A
    generated version (40ants-doc, from the same docstrings) is planned; until it
    lands, treat `common-lisp/src/packages.lisp` as the authoritative symbol
    list and this page as the guide to it.

## Two packages, two lanes

| Package | Nickname | Lane | Transport | Role |
|---------|----------|------|-----------|------|
| `gloopy` | — | Structural (gRPC) | TCP `127.0.0.1:50051` | Create/edit the composition, query state, render |
| `gloopy.osc` | `glosc` | Live (OSC) | UDP `127.0.0.1:9000` | Fire-and-forget notes, CC, knob turns, transport |

`gloopy` **creates and names** things and hands back stable **`id`s**;
`gloopy.osc` **performs** on those ids with the lowest possible latency. Load the
system with `(asdf:load-system :gloopy)` from the `common-lisp/` directory.

Two conventions run through the whole `gloopy` package:

- **Queries return plists.** Every read call (`transport`, `get-state`,
  `list-tracks`, `list-inserts`, `effect-params`, `subscribe`) returns Lisp
  plists — pull fields out with `getf`.
- **Actions signal on failure.** A rejected RPC signals a Lisp error with the
  server's message rather than returning a status code, so you can wrap calls in
  ordinary `handler-case`.

---

## Structural — `gloopy` (gRPC)

```lisp
(asdf:load-system :gloopy)
(in-package :gloopy)
(connect)                                     ; 127.0.0.1:50051
(let ((id (add-synth-track "Lead" :wave :saw)))
  (add-clip id :notes (list (note 60 0 1) (note 64 1 1) (note 67 2 1)))
  (play)
  (subscribe :seconds 3 :on-event #'print)    ; stream playhead + meters
  (stop)
  (render "/tmp/mix.wav"))                     ; offline bounce
```

### Connection

| Function | Purpose |
|----------|---------|
| `(connect &key (host "127.0.0.1") (port 50051))` | Open (or reopen) the channel to a running Gloopy. Returns the channel. |
| `(disconnect)` | Close the channel. |
| `(connectedp)` | `T` when a channel is open. |
| `*channel*` | The active ag-grpc channel, or `NIL`. |

### Transport

| Function | Purpose |
|----------|---------|
| `(play)` / `(stop)` | Start / stop-and-rewind the transport. |
| `(set-tempo bpm)` | Set the tempo in BPM. |
| `(set-swing amount)` | Swing feel: `0.5` straight … `0.75` triplet. |
| `(seek beats)` | Move the playhead to `beats`. |
| `(transport)` | → `(:playing :bpm :position-beats)`. |

### Tempo map

For mid-song tempo changes. An empty map means the transport's single constant
tempo; adding markers makes the beat↔time mapping piecewise (constant between
markers). The render path honours the map. See
[Tempo map & time](../../concepts/model.md#tempo-map-time).

| Function | Purpose |
|----------|---------|
| `(add-tempo-marker beat bpm)` | Set the tempo to `bpm` from `beat` onward (upsert by beat). |
| `(remove-tempo-marker beat)` | Remove the marker at `beat`. |
| `(list-tempo-markers)` | → list of `(:beat :bpm)` plists, sorted by beat. |

```lisp
(add-tempo-marker 0 120)      ; verse at 120
(add-tempo-marker 32 140)     ; lift to 140 at bar 9 (beat 32)
(list-tempo-markers)          ; => ((:BEAT 0.0 :BPM 120.0) (:BEAT 32.0 :BPM 140.0))
```

### Tracks

| Function | Purpose |
|----------|---------|
| `(add-synth-track name &key (wave :saw) attack decay sustain release gain)` | Built-in synth instrument; returns its `id`. |
| `(add-sampler-track name path &key (root-note 60))` | One-shot sampler from a server-side `.wav`; returns its `id`. |
| `(add-sfz-track name path)` | Native SFZ instrument from a server-side path; returns its `id`. |
| `(add-audio-track name)` | Empty audio track (holds audio clips); returns its `id`. |
| `(add-plugin-track identifier)` | Instrument track hosting a plugin (see `list-plugins`); returns its `id`. |
| `(set-track-params id &key volume pan mute solo name)` | Set only the supplied params (see the proto3 caveat below). |
| `(set-synth-param id name value)` | Tweak the built-in synth engine (see param names below). |
| `(remove-track id)` | Delete a track. |
| `(list-tracks)` | → list of `(:id :name :type :volume :pan :mute :clips)`. |
| `(get-state)` | Whole-project snapshot: `(:transport … :tracks (…) :inserts (…))`. |

`wave` is a keyword — one of `:sine :saw :square :triangle` (or the raw integer).
`set-synth-param` names (strings): `wave osc2wave osc2detune oscmix sub attack
decay sustain release gain ftype cutoff reso fenvamt fattack fdecay fsustain
frelease lfotarget lforate lfodepth`.

### Macros (the rack layer)

A **macro** is one perceptual encoder (a single `0..1` knob) mapped onto one or more
underlying params. Each mapping has an authored **safe range** `[lo, hi]`; turning the
macro sweeps every mapped param across its range (`param = lerp(lo, hi, value)`). Because a
macro can only move a param inside that guardrail, `randomize-macros` stays musical. Macros
live on a track and are saved with the project.

| Function | Purpose |
|----------|---------|
| `(add-macro id &optional name)` | Add a macro to a track; returns its index. |
| `(set-macro-value id macro value)` | Turn the macro (`0..1`); every mapped param sweeps its `[lo,hi]`. |
| `(map-macro-synth id macro param &optional lo hi)` | Map onto a built-in synth param (names above), swept `lo..hi`. |
| `(map-macro-effect id macro insert slot param &optional lo hi)` | Map onto a mixer insert-effect param. |
| `(randomize-macros id)` | Roll every macro to a fresh random `0..1` and apply. |
| `(rename-macro id macro name)` | Rename a macro (empty names rejected). |
| `(clear-macro-mappings id macro)` | Remove all of a macro's mappings (keep the macro). |
| `(remove-macro id macro)` | Delete a macro (later macros shift down one index). |

```lisp
(let ((tr (add-synth-track "Lead")))
  (add-macro tr "Brightness")                 ; macro 0
  (map-macro-synth tr 0 "cutoff" 0.2 0.9)     ; open the filter as the knob turns up
  (map-macro-synth tr 0 "reso"   0.0 0.4)     ; …and add a little resonance with it
  (set-macro-value tr 0 0.75)                 ; 3/4 bright
  (randomize-macros tr))                       ; or roll the dice — stays in range
```

### Clips

| Function | Purpose |
|----------|---------|
| `(note pitch start length &optional (velocity 0.8))` | Build a note for `add-clip`: `pitch` a MIDI number or a name (`"C#4"`), `start`/`length` in beats (`length` also takes a `dur` code), `velocity` 0–1. |
| `(seq steps &key (start 0) (velocity 0.8))` | Lay `(pitch duration)` steps end-to-end; a `:rest`/`nil`/`"r"` pitch leaves a gap. Returns a list of notes. |
| `(mini string &key (start 0) (velocity 0.8))` | Parse [mini-notation](../describing-notes.md#mini-the-mini-notation-string) into a list of notes. |
| `(add-clip track-id &key (start 0) (length 4) (content 0) (looped t) notes (name ""))` | Add a MIDI clip; returns `(:track-id :index)`. `content` 0 means = `length`. |
| `(add-audio-clip track-id path &key (start 0) (gain 1.0))` | Import a server-side `.wav` as an audio clip; returns `(:track-id :index)`. |
| `(remove-clip track-id index)` | Delete a clip. |
| `(move-clip track-id index start &key to-track)` | Move a clip along its track, or `:to-track` another track. |

### Note names, durations, scales & chords

Pure client-side helpers for building notes musically. Full syntax — pitch
names, duration codes, scale and chord tables, the mini-notation grammar — is in
[Describing notes](../describing-notes.md).

| Function | Purpose |
|----------|---------|
| `(pitch name-or-midi)` | MIDI number for a name (`"C#4"`, `"Eb3"`; default octave 4); ints pass through. |
| `(pitch-name midi)` | Sharp-spelled name for a MIDI number (`61` → `"C#4"`). |
| `(dur code)` | Beats for a duration code (`"q"` `"h"` `"w"` `"e"` `"s"`, dotted `"q."`, triplet `"8t"`); numbers pass through. |
| `(scale root &optional (name :major) (octaves 1))` | List of MIDI pitches of a named scale from `root`. |
| `(chord root &optional (quality :maj) (inversion 0))` | List of MIDI pitches of a chord above `root`. |

### Mixer & effects

Mixer inserts are indexed; **index 0 is the master bus**. Effects live in each
insert's chain, addressed by `slot`.

| Function | Purpose |
|----------|---------|
| `(list-inserts)` | → list of `(:index :name :volume :pan :mute :solo :effects)`. |
| `(add-effect insert type)` | Add a built-in effect; returns its `slot`. |
| `(add-plugin-effect insert identifier)` | Host a plugin effect (see `list-plugins`); returns its `slot`. |
| `(remove-effect insert slot)` | Delete an effect from the chain. |
| `(set-effect-param insert slot name value)` | Set a named effect parameter. |
| `(set-effect-bypass insert slot bypassed)` | Bypass / re-enable an effect. |
| `(effect-params insert slot)` | → list of `(:name :value :min :max)`. |

`type` is a keyword — one of `:gain :filter :delay :reverb` (or the raw integer).

### Plugins

| Function | Purpose |
|----------|---------|
| `(list-plugins)` | → list of `(:name :format :instrument :identifier)`. |
| `(scan-plugins &optional force)` | Rescan (or restore the cache) and return the plugin list. |
| `(open-plugin-editor track-id)` | Open a hosted plugin's editor window. |

### Project & render

| Function | Purpose |
|----------|---------|
| `(new-project)` | Reset to a fresh project. |
| `(load-project path)` / `(save-project path)` | Load / save the single-file project. |
| `(load-composition path)` / `(save-composition path)` | Load / save the directory ["composition as repo"](../../concepts/model.md#on-disk-two-forms) format. |
| `(render path &key (tail-seconds 2.0))` | Bounce the whole song to a server-side WAV; blocks until done. |

### Events (server stream)

```lisp
(subscribe &key (transport t) (meters t) (interval-ms 200) (seconds 3) on-event)
```

Streams playhead + meter events for roughly `seconds` seconds. Returns the
collected event plists and, if `on-event` is supplied, calls it with each event
as it arrives. Each event is tagged by `:kind`:

- `(:kind :transport :playing :bpm :position-beats)`
- `(:kind :meters :peak-l :peak-r)`

!!! warning "proto3 omits defaults — `set-track-params` can't zero a value"
    Because proto3 drops default (zero / false) fields on the wire,
    `set-track-params` **cannot** set `volume`/`pan` to exactly `0.0`, nor turn
    `mute`/`solo` *off*. For "set to 0" or "turn a flag off", use the OSC lane
    (`glosc:vol`, `glosc:mute`) instead. This is a property of the protocol, not
    the client — see [the model](../../concepts/model.md#the-two-control-lanes).

---

## Live — `gloopy.osc` / `glosc` (OSC)

The live lane is fire-and-forget over UDP. `connect` prepares the socket but does
not confirm anyone is listening; a stray "port unreachable" never aborts a
performance. `with-osc` scopes the connection.

```lisp
(glosc:with-osc ()
  (glosc:note-on 5 60 100)      ; middle C on track 5, right now
  (glosc:chord   5 '(57 60 64)) ; an A-minor chord
  (glosc:cc      5 74 0.5)      ; filter cutoff via CC 74
  (glosc:vol     5 0.8)         ; track volume
  (glosc:fx-param 6 0 "Wet" 0.6); reverb wet on insert 6, slot 0
  (glosc:tempo   128.0)
  (glosc:play))
```

### Connection & lifecycle

| Function | Purpose |
|----------|---------|
| `(connect &key host port)` | Open the UDP socket (defaults `*host*` / `*port*`). |
| `(disconnect)` | Close the socket. |
| `(connectedp)` | `T` when the socket is open. |
| `(with-osc (&key host port) &body body)` | Connect for the duration of `body`, then disconnect. |
| `*host*` / `*port*` / `*transmitter*` | Defaults `127.0.0.1` / `9000`, and the live socket. |

### Live MIDI

| Function | Purpose |
|----------|---------|
| `(note-on track pitch &optional (velocity 100))` | Note-on now. |
| `(note-off track pitch)` | Note-off now. |
| `(chord track pitches &optional (velocity 100))` | Note-on for each pitch in the list. |
| `(cc track controller value)` | A MIDI CC (value 0–1) — the backbone of MIDI-learn. |

### Params & effects

| Function | Purpose |
|----------|---------|
| `(vol track v)` | Track volume (0–1). |
| `(pan track p)` | Track pan (−1…+1). |
| `(mute track &optional (on t))` | Mute / unmute a track. |
| `(fx-param insert slot name value)` | Set a named effect parameter live. |

### Transport & demo

| Function | Purpose |
|----------|---------|
| `(play)` / `(stop)` | Start / stop the transport. |
| `(tempo bpm)` | Set the tempo. |
| `(seek beats)` | Jump to `beats`. |
| `(demo &key (track 5) (velocity 95))` | A tiny built-in arpeggio, for a quick "is it wired up?" check. |

!!! note "OSC is encode-only"
    The Lisp OSC support **encodes** messages onto the wire (message
    construction + type-tagging via the third-party `osc` library; the datagram
    socket is driven with `usocket`). It does not receive — reads come back over
    gRPC. See `common-lisp/README.md` for the current status.

---

## Tests

Offline — no running Gloopy required. They check the OSC wire encoding
byte-for-byte against an independent reference and exercise the pure gRPC-side
helpers:

```lisp
(asdf:test-system :gloopy)
```

---

*Related:* [Common Lisp quickstart](../../tutorials/lisp-quickstart.md) ·
[The Gloopy model](../../concepts/model.md) ·
[gRPC reference](../grpc/index.md) · [OSC reference](../osc/index.md)
