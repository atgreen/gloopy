# Describing notes

Helpers for writing notes the way musicians name them — pitches, durations,
scales, chords — instead of by raw MIDI number and beat count. They are pure:
they build the same [`Note`](../concepts/model.md#note) values `note` produces,
run entirely client-side, and talk to nothing. Use them anywhere you assemble a
clip's notes, then hand the result to `add_clip` / `add-clip`.

The same vocabulary is available in both clients (Python `gloopy.music`,
re-exported from the package; Common Lisp package `gloopy`) and matches the
built-in scale names the desktop app uses.

## Pitches

A pitch is a **MIDI number** (0–127) or a **name** in scientific pitch notation.
`note` and every helper below accept either.

| Form | Examples | Notes |
|------|----------|-------|
| MIDI number | `60`, `72` | Passed through unchanged. |
| Name | `"C4"`, `"c4"`, `"F#3"`, `"Bb5"` | Case-insensitive letter, then accidentals, then octave. |
| Accidentals | `"C#4"` / `"Db4"`, `"C##4"` | `#` sharp, `b` flat; may repeat. |
| Octave | `"C"` (= `"C4"`), `"C-1"`, `"G9"` | Optional; **defaults to 4**. `C4 = 60`, so `C-1 = 0` and `G9 = 127`. |

=== "Python"

    ```python
    from gloopy import pitch, pitch_name
    pitch("C#4")     # -> 61
    pitch(60)        # -> 60
    pitch_name(61)   # -> "C#4"   (sharp-spelled)
    ```

=== "Common Lisp"

    ```lisp
    (pitch "C#4")    ; => 61
    (pitch 60)       ; => 60
    (pitch-name 61)  ; => "C#4"   (sharp-spelled)
    ```

## Durations

A duration is a **length in beats** (a quarter note = 1 beat) or a **shorthand
code**. A trailing `.` dots the value (stackable); a trailing `t` makes it a
triplet.

| Code | Beats | Code | Beats |
|------|-------|------|-------|
| `w` whole | 4 | `s` / `16` sixteenth | 0.25 |
| `h` half | 2 | `32` thirty-second | 0.125 |
| `q` quarter | 1 | `q.` dotted quarter | 1.5 |
| `e` / `8` eighth | 0.5 | `q..` double-dotted | 1.75 |
| `8t` eighth-triplet | ⅓ | *(a number)* | itself, in beats |

=== "Python"

    ```python
    from gloopy import dur
    dur("q")    # -> 1.0
    dur("8t")   # -> 0.333…
    dur(0.25)   # -> 0.25
    ```

=== "Common Lisp"

    ```lisp
    (dur "q")   ; => 1
    (dur "8t")  ; => 1/3
    (dur 0.25)  ; => 0.25
    ```

## Building a note

`note` takes a pitch and a length in either form; the rest is start position
(beats, relative to the clip) and velocity.

=== "Python"

    ```python
    from gloopy import note
    note("E4", 1, "q")            # E4 at beat 1, a quarter long
    note(60, 0, 1, velocity=0.9)  # equivalent to note("C4", 0, "q", 0.9)
    ```

=== "Common Lisp"

    ```lisp
    (note "E4" 1 "q")             ; E4 at beat 1, a quarter long
    (note 60 0 1 0.9)             ; equivalent to (note "C4" 0 "q" 0.9)
    ```

## `seq` — steps laid end-to-end

`seq` places a list of `(pitch, duration)` steps one after another, advancing the
clock by each step's duration. A step may carry its own velocity, and a **rest**
(`REST` / `None` / `"r"` in Python; `:rest` / `nil` / `"r"` in Lisp) leaves a gap
without sounding.

=== "Python"

    ```python
    from gloopy import seq, REST
    seq([("C4","q"), ("E4","e"), (REST,"e"), ("G4","h")])
    ```

=== "Common Lisp"

    ```lisp
    (seq '(("C4" "q") ("E4" "e") (:rest "e") ("G4" "h")))
    ```

## `mini` — the mini-notation string

`mini` parses a compact string: one whitespace-separated token per event. A
duration glues onto the pitch with **no separator** and is **sticky** — omit it
and the event keeps the previous length (a quarter to start). Because durations
are letters, digits stay free for absolute octaves.

| Token | Means |
|-------|-------|
| `c4q` | A note: pitch then optional duration suffix. |
| `d` `e` `f` | Bare notes inherit the running duration (default octave 4). |
| `r` / `_` (`re`, `r8`) | A rest; advances the clock, sounds nothing. |
| `[c e g]q` | A chord — pitches inside `[…]` (space-separated) share one start and length. |
| `c4q.` `c4et` | Dotted and triplet durations (any `dur` code). |

```text
c4q d e f            four quarter notes: C4 D4 E4 F4
c4q d e f  g4h a b   C..F quarter, then G A B half
[c e g]q [f a c5]h   two chords
c4e r e f            eighths with a rest in the middle
```

=== "Python"

    ```python
    from gloopy import mini
    mini("c4q d e f  g4h a b")
    ```

=== "Common Lisp"

    ```lisp
    (mini "c4q d e f  g4h a b")
    ```

There are no standalone duration tokens: to change length, attach the code to
the note that starts the run. A bare `e` is therefore always the note E — the
eighth note is written attached (`ce`, `c4e`).

## Scales

`scale` returns the MIDI pitches of a named scale, ascending from a root over one
or more octaves. Names — and their semitone offsets — match the app's built-in
scales.

| Name | Offsets | Name | Offsets |
|------|---------|------|---------|
| `major` / `ionian` | 0 2 4 5 7 9 11 | `mixolydian` | 0 2 4 5 7 9 10 |
| `minor` / `aeolian` | 0 2 3 5 7 8 10 | `locrian` | 0 1 3 5 6 8 10 |
| `harmonic-minor` | 0 2 3 5 7 8 11 | `pentatonic-major` | 0 2 4 7 9 |
| `melodic-minor` | 0 2 3 5 7 9 11 | `pentatonic-minor` | 0 3 5 7 10 |
| `dorian` | 0 2 3 5 7 9 10 | `blues` | 0 3 5 6 7 10 |
| `phrygian` | 0 1 3 5 7 8 10 | `whole-tone` | 0 2 4 6 8 10 |
| `lydian` | 0 2 4 6 7 9 11 | `chromatic` | 0 1 2 3 4 5 6 7 8 9 10 11 |

=== "Python"

    ```python
    from gloopy import scale
    scale("C4", "major")        # [60, 62, 64, 65, 67, 69, 71]
    scale("C4", "major", 2)     # two octaves (14 pitches)
    ```

=== "Common Lisp"

    ```lisp
    (scale "C4" :major)         ; (60 62 64 65 67 69 71)
    (scale "C4" :major 2)       ; two octaves (14 pitches)
    ```

## Chords

`chord` returns the MIDI pitches of a chord above a root. `inversion` raises that
many of the lowest notes by an octave.

| Quality | Offsets | Quality | Offsets |
|---------|---------|---------|---------|
| `maj` (`major`, `""`) | 0 4 7 | `7` / `dom7` | 0 4 7 10 |
| `min` (`m`) | 0 3 7 | `dim7` | 0 3 6 9 |
| `dim` | 0 3 6 | `m7b5` / `half-dim` | 0 3 6 10 |
| `aug` | 0 4 8 | `6` | 0 4 7 9 |
| `sus2` / `sus4` | 0 2 7 / 0 5 7 | `min6` (`m6`) | 0 3 7 9 |
| `maj7` | 0 4 7 11 | `9` / `maj9` / `min9` | +14 |
| `min7` (`m7`) | 0 3 7 10 | `add9` / `5` | 0 4 7 14 / 0 7 |

=== "Python"

    ```python
    from gloopy import chord
    chord("C4", "maj7")        # [60, 64, 67, 71]
    chord("C4", "maj", 1)      # first inversion -> [64, 67, 72]
    ```

=== "Common Lisp"

    ```lisp
    (chord "C4" :maj7)         ; (60 64 67 71)
    (chord "C4" :maj 1)        ; first inversion => (64 67 72)
    ```

## Return values

`note`, `seq` and `mini` each return an ordinary **list of `Note` values** — the
same type throughout — so you can iterate, transform, filter, and combine before
adding a clip. `scale` and `chord` return **lists of MIDI numbers**.

=== "Python"

    ```python
    notes = mini("c4q d e f")            # list[Note]
    up = [note(n.pitch + 7, n.start_beat, n.length_beats, n.velocity)
          for n in notes]               # transpose up a fifth
    g.add_clip(lead, notes=up + mini("[c e g]w", start=4))
    ```

=== "Common Lisp"

    ```lisp
    (let ((notes (mini "c4q d e f")))    ; a list of notes
      (add-clip lead
                :notes (mapcar (lambda (n)                 ; transpose up a fifth
                                 (note (+ (gloopy.pb::pitch n) 7)
                                       (gloopy.pb::start-beat n)
                                       (gloopy.pb::length-beats n)))
                               notes)))
    ```

*Related:* [Note](../concepts/model.md#note) ·
[Generate a clip's notes from code](../../user-guide/how-to/generate-notes-from-code.md) ·
[Python client](python/index.md) · [Common Lisp client](lisp/index.md)
