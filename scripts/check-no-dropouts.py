#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Headless RT-safety regression check: assert STEADY-STATE playback drops no audio blocks.

Plays a project past its cold first pass (sfizz sample-streaming warms up on the first
encounter of each region — a one-time transient, not a regression), then measures a
steady-state window and fails if the engine dropped any blocks (`diagnostics()['dropouts']`
grew). A dropout means the audio callback's try-lock on `engineLock` missed — i.e. a
message-thread holder stalled the audio thread. When it fails, re-run Gloopy with
GLOOPY_TRACE_DROPOUTS=1 and its stdout will carry a `[dropout] engineLock held by
<file:line> for <us>us` line naming the culprit (see Source/EngineLock.h).

Requires a running Gloopy (desktop or headless) reachable on the control port.

    python3 scripts/check-no-dropouts.py --load examples/demo-ascension
    python3 scripts/check-no-dropouts.py --warmup 75 --measure 30   # tune the windows

Exit 0 = steady-state clean; exit 1 = dropouts in the measured window (regression).
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
from gloopy import Gloopy  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--load", metavar="PATH", help="load this project first")
    ap.add_argument("--warmup", type=float, default=75.0,
                    help="seconds to play before measuring, to clear the cold first pass "
                         "(default 75 ~ one pass of demo-ascension)")
    ap.add_argument("--measure", type=float, default=30.0,
                    help="steady-state measurement window in seconds (default 30)")
    ap.add_argument("--target", default=None, help="host:port (default: auto-discover)")
    args = ap.parse_args()

    with Gloopy(args.target) as g:
        if args.load:
            g.load_project(os.path.abspath(args.load))
            time.sleep(2.0)
        g.seek(0)
        g.play()
        print(f"warming up {args.warmup:.0f}s (cold first pass is not a regression)...")
        time.sleep(args.warmup)

        base = g.diagnostics()
        print(f"measuring steady-state for {args.measure:.0f}s ...")
        time.sleep(args.measure)
        end = g.diagnostics()
        g.stop()

        dropped = end["dropouts"] - base["dropouts"]
        allocs = end["audio_thread_allocs"] - base["audio_thread_allocs"]
        print(f"steady-state window: dropouts +{dropped}, audio-thread allocs +{allocs}, "
              f"peak dsp_load {end['dsp_load']*100:.0f}%")

        ok = dropped == 0 and allocs == 0
        if ok:
            print("PASS: steady-state playback is clean (0 dropouts, 0 allocs).")
            return 0
        print("FAIL: steady-state playback dropped blocks or allocated on the audio thread.")
        print("      Re-run Gloopy with GLOOPY_TRACE_DROPOUTS=1 to log a '[dropout] engineLock "
              "held by <file:line> for <us>us' line naming the holder (see Source/EngineLock.h).")
        return 1


if __name__ == "__main__":
    sys.exit(main())
