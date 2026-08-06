#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Crackle probe — classify audio dropouts by sampling the engine diagnostics
before/during/after playback of a running Gloopy project.

Gloopy's audio callback separates three distinct crackle modes (see
Source/MainComponent.cpp getNextAudioBlock + Source/Diagnostics.cpp):

  * dropouts            -> the block was SKIPPED because the message thread held
                           `engineLock` (a ScopedTryLock miss -> silence). Rising
                           during steady playback == lock contention.
  * dsp_load / callback -> callback time vs the block's real-time budget. Peaks
                           near / over 1.0 == CPU/voice overload (too many sfizz
                           voices, a heavy effect, buffer too small).
  * audio_thread_allocs -> a heap allocation happened INSIDE the mix (RT-safety
                           violation, e.g. a sample streamed/allocated on the
                           audio thread when a note first triggers).

`dropouts` and `audio_thread_allocs` are cumulative counters, so we delta them
across the window. `max_callback_us` is a never-reset running max, so we note it
before/after AND track the peak of the instantaneous `callback_us` we sample.

Usage:
    python3 scripts/crackle-probe.py                 # probe the already-open project
    python3 scripts/crackle-probe.py --load examples/demo-ascension
    python3 scripts/crackle-probe.py --seconds 30 --interval 0.1 --seek 0

Requires a running Gloopy (desktop or headless). Auto-discovers the control port.
"""
import argparse
import os
import sys
import time

# Allow running straight from the repo without installing the package.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
from gloopy import Gloopy  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--load", metavar="PATH",
                    help="load this project first (else probe whatever is open)")
    ap.add_argument("--seconds", type=float, default=20.0,
                    help="how long to sample during playback (default 20)")
    ap.add_argument("--interval", type=float, default=0.1,
                    help="seconds between samples (default 0.1)")
    ap.add_argument("--seek", type=float, default=None,
                    help="seek to this beat before playing (e.g. 0)")
    ap.add_argument("--no-stop", action="store_true",
                    help="leave transport playing when done")
    ap.add_argument("--target", default=None, help="host:port (default: auto-discover)")
    args = ap.parse_args()

    with Gloopy(args.target) as g:
        if args.load:
            print(f"Loading project: {args.load}")
            g.load_project(os.path.abspath(args.load))
            time.sleep(1.0)  # let instruments/plugins prepare before we baseline

        base = g.diagnostics()
        sr = base["sample_rate"] or 0.0
        block = base["block_size"] or 0
        budget_us = (block / sr * 1e6) if sr > 0 and block > 0 else 0.0
        print(f"\nDevice: {sr:.0f} Hz, block {block} samples, "
              f"{base['inputs']} in / {base['outputs']} out")
        print(f"Real-time budget per block: {budget_us:.1f} us "
              f"({block} / {sr:.0f} Hz)\n")

        d0_dropouts = base["dropouts"]
        d0_allocs = base["audio_thread_allocs"]
        max_cb_before = base["max_callback_us"]

        if args.seek is not None:
            g.seek(args.seek)
        print(f"Playing and sampling for {args.seconds:.0f}s "
              f"every {args.interval*1000:.0f}ms ...")
        g.play()

        peak_cb = 0.0
        peak_load = 0.0
        loads = []
        samples = 0
        deadline = args.seconds
        t_start = time.monotonic()
        try:
            while time.monotonic() - t_start < deadline:
                d = g.diagnostics()
                cb = d["callback_us"]
                load = d["dsp_load"]
                peak_cb = max(peak_cb, cb)
                peak_load = max(peak_load, load)
                loads.append(load)
                samples += 1
                # live one-liner: current load + accumulated dropouts/allocs delta
                sys.stdout.write(
                    f"\r  load {load*100:5.1f}%  peak {peak_load*100:5.1f}%  "
                    f"dropouts +{d['dropouts']-d0_dropouts:<6d} "
                    f"allocs +{d['audio_thread_allocs']-d0_allocs:<8d}")
                sys.stdout.flush()
                time.sleep(args.interval)
        finally:
            if not args.no_stop:
                g.stop()
        print()

        final = g.diagnostics()
        dropouts = final["dropouts"] - d0_dropouts
        allocs = final["audio_thread_allocs"] - d0_allocs
        max_cb_after = final["max_callback_us"]
        mean_load = sum(loads) / len(loads) if loads else 0.0

        print("\n" + "=" * 60)
        print("RESULT")
        print("=" * 60)
        print(f"  samples taken        : {samples}")
        print(f"  dropouts (delta)     : {dropouts}")
        print(f"  audio allocs (delta) : {allocs}")
        print(f"  dsp_load mean/peak   : {mean_load*100:.1f}% / {peak_load*100:.1f}%")
        print(f"  callback_us peak seen: {peak_cb:.1f} us  (budget {budget_us:.1f} us)")
        print(f"  max_callback_us      : {max_cb_before:.1f} -> {max_cb_after:.1f} us")
        print()

        # ---- verdict -----------------------------------------------------
        # A single sampled block can legitimately be slow; what matters is
        # which signal actually moved. Report all that fired.
        verdicts = []
        if dropouts > 0:
            verdicts.append(
                f"LOCK CONTENTION: {dropouts} block(s) skipped -> silence. The "
                f"message thread is holding engineLock across a callback. Look for "
                f"a heavy ScopedLock(engineLock) holder firing during playback "
                f"(automation apply, param sync, a UI op doing real work).")
        if allocs > 0:
            verdicts.append(
                f"AUDIO-THREAD ALLOC: {allocs} heap allocation(s) inside the mix. "
                f"Something allocates in the render path (likely sfizz streaming / "
                f"voice allocation on first note-on). This both risks a page-fault "
                f"stall AND is a principle-4 violation.")
        if peak_load >= 1.0 or max_cb_after > max_cb_before:
            verdicts.append(
                f"CPU OVERLOAD: callback exceeded (or nearly exceeded) its "
                f"{budget_us:.0f}us budget (peak load {peak_load*100:.0f}%). Too "
                f"many concurrent voices / a heavy effect for this buffer size. "
                f"Try a larger block (see scripts/setup-realtime-audio.sh) to "
                f"confirm, then reduce voices or optimise the hot effect.")
        elif peak_load >= 0.8:
            verdicts.append(
                f"CPU MARGINAL: peak load {peak_load*100:.0f}% — close to the edge, "
                f"transient spikes may already be causing occasional glitches.")

        if verdicts:
            for i, v in enumerate(verdicts, 1):
                print(f"[{i}] {v}\n")
        else:
            print("No dropouts, no allocs, comfortable CPU headroom in this window.")
            print("If it still crackled, try: a longer --seconds, --seek to the")
            print("busiest section, or reproduce the exact UI action that triggers it")
            print("(loading a plugin, adding a track) while this probe runs.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
