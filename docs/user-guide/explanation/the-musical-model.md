# The musical model

Why Gloopy measures everything in **beats**, what the tempo map does, and how
scales and tuning sit on top. This is background — for the exact definitions, see
[The Gloopy model](../../control-scripting/concepts/model.md).

## Everything is in beats — and a beat is a quarter note

Every position and length in Gloopy — a note's start, a clip's length, a marker, a
loop, an automation point — is expressed in **beats**, and **a beat is always a
quarter note**. Not "one count of the current time signature", not seconds: a
fixed, unambiguous quarter note.

That single choice buys a lot:

- **Tempo-independence.** A note at beat 4 is at beat 4 whether the song is 90 BPM
  or 140. Change the tempo and the music stretches in time without any position
  being rewritten.
- **One unit everywhere.** The piano roll, the step grid, automation, the API, and
  the file format all speak beats. There's no conversion layer to get wrong.
- **Stable files.** Because clips store beats, not seconds or samples, the
  composition text doesn't churn when you change the tempo — the numbers are the
  same.

Notes are quarter-note-relative *to their clip*: a note's `start` is measured from
the clip's beginning, and the clip's position on the timeline is where it lives in
the song. Move the clip and every note moves with it, untouched.

## Tempo and the tempo map

Most songs have one tempo — a single BPM. But a composition can also hold a
**[tempo map](../../control-scripting/concepts/model.md#tempo-map-time)**: a sorted
list of `(beat, bpm)` markers. Between two markers the tempo is constant, so the
map is *piecewise* — a beat span integrates to seconds segment by segment. Before
the first marker, the first marker's tempo applies; an empty map just means the
one constant tempo.

This is the bridge between the two worlds: **beats** are the musical grid, but your
ears (and the rendered WAV) live in **seconds**. The tempo map is the exact
conversion between them, and the render honours it — so a mid-song accelerando
comes out right.

## Time signature and the bar grid

The **time signature** sets how many beats make a bar — it drives the bar ruler and
the `bar . beat . tick` position readout. It does **not** change the beat unit: 3/4
and 4/4 both count in quarter notes; they differ only in where the barlines fall.
That's why the position display and the underlying beat numbers can be reasoned
about separately.

A piece can also **change meter partway through** — a 4/4 verse into a 6/8 bridge.
Like the tempo map, this is a sorted list of changes, each taking effect at a bar
line; the ruler renumbers the bars and spaces the barlines accordingly from that
point on, and the metronome accents the new downbeats. Right-click the arrangement
ruler to add or remove a change (or set the initial, bar-1 signature). Because clip
and note positions are stored in beats — not bars — inserting a meter change never
moves your existing material; only the barlines drawn over it move.

## Scales and tuning

Two more layers sit on top of the beat grid, both optional:

- A project **scale** — a root and a scale type (major, minor, …). It highlights
  in-scale rows in the piano roll and powers **snap-to-scale** when you draw or
  move notes. It's an aid to writing, not a constraint on what can sound.
- A **tuning** — by default 12-tone equal temperament, but you can load a Scala
  `.scl` file for microtuning, which retunes the pitches themselves.

Scale is about *which notes you pick*; tuning is about *what frequency each note
is*. They're independent.

## See also

- [The Gloopy model](../../control-scripting/concepts/model.md) — the precise
  definitions of transport, tempo map, note, and scale.
- [How playback is scheduled](how-playback-is-scheduled.md) — how these beats turn
  into sound over time.
