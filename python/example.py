#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Build a tiny arpeggio over the control API and render it — the Python twin of
examples/gloopy-grpc.lisp. Requires a running Gloopy (`./build/.../Gloopy`)."""
import sys
from gloopy import Gloopy, note

with Gloopy() as g:
    g.new_project()
    g.set_tempo(120)
    g.set_swing(0.62)                          # a little shuffle

    lead = g.add_synth_track("lead", wave="SAW", attack=0.01, release=0.25, gain=0.85)
    g.add_effect(0, "REVERB")                  # master reverb (insert 0 = master)

    # A one-bar rising arpeggio, four times.
    pattern = [note(p, i * 0.5, 0.5, 0.9)
               for i, p in enumerate([60, 64, 67, 71, 72, 71, 67, 64])]
    g.add_clip(lead, notes=pattern, length_beats=4, content_len_beats=4)

    print("tracks:", g.list_tracks())
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/gloopy-python-demo.wav"
    g.render(out, tail_seconds=1.5, start_beat=0, end_beat=4)
    print("rendered", out)
