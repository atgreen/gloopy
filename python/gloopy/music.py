# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Music-theory sugar for building Gloopy clips: note names, durations,
scales and chords.

Nothing here talks to the DAW — these are pure helpers that turn musician
vocabulary into the ints and beats the wire format wants. A pitch is a MIDI
number (C4 = 60, the same middle-C Gloopy uses for a sampler's root_note); a
duration is a length in beats (a quarter note = 1 beat).

    from gloopy import Gloopy, seq, scale, chord

    g = Gloopy()
    tid = g.add_synth_track("lead", wave="SAW")

    # a melody, note names + duration shorthand, laid end-to-end:
    g.add_clip(tid, notes=seq([("C4","q"), ("E4","e"), ("G4","e"),
                               (None,"q"), ("A4","h")]))

    # a C-major run and a Cmaj7 stab:
    run  = [note(p, i*0.25, 0.25) for i, p in enumerate(scale("C4", "major"))]
    stab = [note(p, 0, 2) for p in chord("C4", "maj7")]

The scale names match Gloopy's engine (``Source/Scales.cpp``) so the desktop
"set scale" control, the Python client and the Common Lisp client all speak
one dialect.
"""
from __future__ import annotations

from typing import Iterable, List, Optional, Sequence, Union

# A pitch argument is either a MIDI number or a name like "C#4" / "Eb3".
PitchLike = Union[int, str]
# A duration argument is either a length in beats or a shorthand like "q." / "8t".
DurLike = Union[int, float, str]

# ---- note names <-> MIDI ---------------------------------------------------
# Scientific pitch notation: C4 = 60, so MIDI 0 = C-1 and 127 = G9.

_PC = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
_SHARP_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def pitch(p: PitchLike) -> int:
    """MIDI number for a note name (``"C4"``, ``"F#3"``, ``"Bb5"``); passes ints
    through unchanged. Accidentals may repeat (``"C##4"``); octave is optional
    and defaults to 4 (so ``"C"`` is middle C)."""
    if isinstance(p, bool):        # bool is an int subclass — reject it explicitly
        raise TypeError("pitch must be a MIDI int or a name string, not bool")
    if isinstance(p, int):
        return p
    s = p.strip()
    if not s:
        raise ValueError("empty pitch name")
    letter = s[0].lower()
    if letter not in _PC:
        raise ValueError(f"bad note name: {p!r}")
    semis = _PC[letter]
    i = 1
    while i < len(s) and s[i] in "#b":
        semis += 1 if s[i] == "#" else -1
        i += 1
    octave = int(s[i:]) if i < len(s) else 4
    return (octave + 1) * 12 + semis


def pitch_name(midi: int) -> str:
    """Sharp-spelled scientific name for a MIDI number (``61`` -> ``"C#4"``)."""
    return f"{_SHARP_NAMES[midi % 12]}{midi // 12 - 1}"


# ---- durations -------------------------------------------------------------
# Length in beats, quarter note = 1. Shorthand: whole/half/quarter/eighth/...
# a trailing "." dots (x1.5, stackable), a trailing "t" makes a triplet (x2/3).

_BASE = {
    "w": 4.0, "whole": 4.0,
    "h": 2.0, "half": 2.0,
    "q": 1.0, "quarter": 1.0,
    "e": 0.5, "eighth": 0.5, "8": 0.5,
    "s": 0.25, "sixteenth": 0.25, "16": 0.25,
    "32": 0.125, "thirtysecond": 0.125,
    "64": 0.0625,
}


def dur(d: DurLike) -> float:
    """Length in beats for a duration shorthand; passes numbers through.

    ``dur("q") == 1.0``, ``dur("8") == 0.5``, ``dur("q.") == 1.5`` (dotted),
    ``dur("8t") == 1/3`` (triplet). Dots stack: ``"q.." == 1.75``."""
    if isinstance(d, bool):
        raise TypeError("duration must be a number or shorthand string, not bool")
    if isinstance(d, (int, float)):
        return float(d)
    s = d.strip().lower()
    triplet = s.endswith("t")
    if triplet:
        s = s[:-1]
    dots = 0
    while s.endswith("."):
        dots += 1
        s = s[:-1]
    if s not in _BASE:
        raise ValueError(f"bad duration: {d!r}")
    beats = _BASE[s]
    # each dot adds half of the running total (1 dot x1.5, 2 dots x1.75, ...)
    add = beats
    for _ in range(dots):
        add /= 2.0
        beats += add
    if triplet:
        beats *= 2.0 / 3.0
    return beats


# ---- scales ----------------------------------------------------------------
# Semitone offsets from the root. Mirrors Source/Scales.cpp exactly.

SCALES = {
    "major": [0, 2, 4, 5, 7, 9, 11], "ionian": [0, 2, 4, 5, 7, 9, 11],
    "minor": [0, 2, 3, 5, 7, 8, 10], "aeolian": [0, 2, 3, 5, 7, 8, 10],
    "harmonic-minor": [0, 2, 3, 5, 7, 8, 11],
    "melodic-minor": [0, 2, 3, 5, 7, 9, 11],
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "phrygian": [0, 1, 3, 5, 7, 8, 10],
    "lydian": [0, 2, 4, 6, 7, 9, 11],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10],
    "locrian": [0, 1, 3, 5, 6, 8, 10],
    "pentatonic-major": [0, 2, 4, 7, 9],
    "pentatonic-minor": [0, 3, 5, 7, 10],
    "blues": [0, 3, 5, 6, 7, 10],
    "whole-tone": [0, 2, 4, 6, 8, 10],
    "chromatic": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
}


def scale(root: PitchLike, name: str = "major", octaves: int = 1) -> List[int]:
    """MIDI pitches of a scale, ascending from ``root`` over ``octaves``.

    ``scale("C4", "major")`` -> the seven notes C4..B4. Names match the engine
    (``major``, ``dorian``, ``pentatonic-minor``, ``blues``, ...)."""
    key = name.lower().strip()
    if key not in SCALES:
        raise ValueError(f"unknown scale {name!r}; known: {', '.join(sorted(SCALES))}")
    base = pitch(root)
    steps = SCALES[key]
    return [base + 12 * o + s for o in range(octaves) for s in steps]


# ---- chords ----------------------------------------------------------------
# Semitone offsets from the root. Aliases cover the usual spellings.

CHORDS = {
    "maj": [0, 4, 7], "major": [0, 4, 7], "": [0, 4, 7],
    "min": [0, 3, 7], "minor": [0, 3, 7], "m": [0, 3, 7],
    "dim": [0, 3, 6], "aug": [0, 4, 8],
    "sus2": [0, 2, 7], "sus4": [0, 5, 7],
    "maj7": [0, 4, 7, 11], "M7": [0, 4, 7, 11],
    "min7": [0, 3, 7, 10], "m7": [0, 3, 7, 10],
    "7": [0, 4, 7, 10], "dom7": [0, 4, 7, 10],
    "dim7": [0, 3, 6, 9], "m7b5": [0, 3, 6, 10], "half-dim": [0, 3, 6, 10],
    "6": [0, 4, 7, 9], "min6": [0, 3, 7, 9], "m6": [0, 3, 7, 9],
    "9": [0, 4, 7, 10, 14], "maj9": [0, 4, 7, 11, 14], "min9": [0, 3, 7, 10, 14],
    "add9": [0, 4, 7, 14], "5": [0, 7],
}


def chord(root: PitchLike, quality: str = "maj", inversion: int = 0) -> List[int]:
    """MIDI pitches of a chord. ``chord("C4", "maj7")`` -> [60, 64, 67, 71].

    ``inversion`` raises that many of the lowest notes by an octave (1 = first
    inversion). Qualities: maj, min, dim, aug, sus2/4, maj7, min7, 7, 6, 9, ..."""
    key = quality.strip()
    if key not in CHORDS:
        raise ValueError(f"unknown chord {quality!r}; known: {', '.join(sorted(CHORDS))}")
    base = pitch(root)
    notes = [base + s for s in CHORDS[key]]
    for _ in range(inversion):
        notes = notes[1:] + [notes[0] + 12]
    return notes


# ---- rests + the sequence builder ------------------------------------------
# A rest is a gap in time with no note. In seq(), a step whose pitch is REST
# (or None, or "r"/"rest"/"-") advances the clock without emitting a note.

REST = None
_REST_WORDS = {"r", "rest", "-", "_"}


def _is_rest(p) -> bool:
    return p is REST or (isinstance(p, str) and p.strip().lower() in _REST_WORDS)


def seq(steps: Iterable[Sequence],
        start: float = 0.0,
        velocity: float = 0.8,
        note_builder=None) -> list:
    """Lay ``steps`` end-to-end on the timeline and return a list of Notes.

    Each step is ``(pitch, duration)`` or ``(pitch, duration, velocity)``. A
    step's ``duration`` advances the clock whether or not it sounds, so rests
    (``REST``/``None``/``"r"``) just leave a gap. Pitches accept names, durations
    accept shorthand::

        seq([("C4","q"), ("E4","e"), (REST,"e"), ("G4","h")])

    ``note_builder`` defaults to ``gloopy.note``; pass your own to build a
    different message type (it is called ``builder(pitch, start, length, vel)``)."""
    if note_builder is None:
        from .client import note as note_builder
    out = []
    t = float(start)
    for step in steps:
        p, d = step[0], step[1]
        vel = step[2] if len(step) > 2 else velocity
        length = dur(d)
        if not _is_rest(p):
            out.append(note_builder(pitch(p), t, length, vel))
        t += length
    return out


__all__ = [
    "pitch", "pitch_name", "dur", "scale", "chord", "seq", "REST",
    "SCALES", "CHORDS", "PitchLike", "DurLike",
]
