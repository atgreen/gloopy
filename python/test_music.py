# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Offline tests for gloopy.music — note names, durations, scales, chords.

Hermetic: imports the pure helper module directly, so it needs no running
Gloopy and no grpc/protobuf install. Run with ``python3 test_music.py`` or
``pytest``. Mirrors common-lisp/tests/tests.lisp so both clients agree.
"""
import importlib.util
import os

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "gloopy_music", os.path.join(_here, "gloopy", "music.py"))
m = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(m)


def test_pitch_names():
    assert m.pitch("C4") == 60
    assert m.pitch("C") == 60                 # octave defaults to 4
    assert m.pitch("C#4") == 61
    assert m.pitch("Db4") == 61               # enharmonic
    assert m.pitch("C-1") == 0
    assert m.pitch("G9") == 127
    assert m.pitch(60) == 60                  # int passthrough
    assert m.pitch_name(61) == "C#4"
    assert m.pitch_name(60) == "C4"
    for bad in ("H4", ""):
        try:
            m.pitch(bad)
        except ValueError:
            pass
        else:
            raise AssertionError(f"pitch({bad!r}) should raise")


def test_durations():
    assert m.dur("q") == 1.0
    assert m.dur("8") == 0.5
    assert m.dur("w") == 4.0
    assert m.dur("q.") == 1.5                 # dotted
    assert m.dur("q..") == 1.75               # double-dotted
    assert abs(m.dur("8t") - 1 / 3) < 1e-9    # triplet
    assert m.dur(0.25) == 0.25                # number passthrough
    try:
        m.dur("z")
    except ValueError:
        pass
    else:
        raise AssertionError("dur('z') should raise")


def test_scales_chords():
    assert m.scale("C4", "major") == [60, 62, 64, 65, 67, 69, 71]
    assert m.scale("C4", "pentatonic-minor") == [60, 63, 65, 67, 70]
    assert len(m.scale("C4", "major", 2)) == 14
    assert m.chord("C4", "maj7") == [60, 64, 67, 71]
    assert m.chord("C4", "min") == [60, 63, 67]
    assert m.chord("C4", "maj", 1) == [64, 67, 72]   # first inversion
    for fn, arg in ((m.scale, "bogus"), (m.chord, "bogus")):
        try:
            fn("C4", arg)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{fn.__name__} should reject {arg!r}")


def test_seq():
    # rests advance the clock without emitting a note; capture (pitch,start,len,vel)
    rows = m.seq([("C4", "q"), ("E4", "e"), (m.REST, "e"), ("G4", "h")],
                 note_builder=lambda p, s, l, v: (p, s, l, v))
    assert rows == [(60, 0.0, 1.0, 0.8), (64, 1.0, 0.5, 0.8), (67, 2.0, 2.0, 0.8)]
    # "r" string is a rest too; per-step velocity overrides the default
    rows2 = m.seq([("C4", "q", 0.5), ("r", "q"), ("E4", "q")],
                  note_builder=lambda p, s, l, v: (p, s, v))
    assert rows2 == [(60, 0.0, 0.5), (64, 2.0, 0.8)]


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"{name}: OK")
    print("all python music tests passed")
