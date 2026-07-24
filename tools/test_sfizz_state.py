# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Round-trip tests for the sfizz-state codec (tools/sfizz-state.py).

Run directly: `python3 tools/test_sfizz_state.py`. No pytest dependency —
plain asserts, non-zero exit on failure, so CI can call it as-is.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The module filename has a hyphen, so load it by path.
spec = importlib.util.spec_from_file_location("sfizz_state", os.path.join(HERE, "sfizz-state.py"))
ss = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ss)


def test_juce_b64_roundtrip():
    for payload in (b"", b"\x00", b"hello world", bytes(range(256)), b"\xff" * 100):
        enc = ss.juce_b64_encode(payload)
        dec = ss.juce_b64_decode(enc)
        assert dec == payload, f"b64 round-trip failed for {payload!r}"


def test_reference_state_path_and_retarget():
    ref = os.path.join(HERE, "sfizz-reference-state.txt")
    if not os.path.exists(ref):
        print("  (skipping retarget test: reference state not present)")
        return
    pstate = open(ref).read().strip()

    original = ss.get_path(pstate)
    assert original, "reference state should carry a non-empty SFZ path"

    new_path = "/opt/soundfonts/Piano/Salamander.sfz"
    retargeted = ss.retarget(pstate, new_path)
    assert ss.get_path(retargeted) == new_path, "retarget must update the embedded SFZ path"

    # Retargeting back to the original must reproduce the original path (idempotent codec).
    back = ss.retarget(retargeted, original)
    assert ss.get_path(back) == original


def main():
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{'All sfizz-state tests passed.' if not failures else f'{failures} failure(s).'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
