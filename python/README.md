<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Gloopy Python client

A thin, Pythonic wrapper over Gloopy's gRPC control API — the twin of
[`examples/gloopy-grpc.lisp`](../examples/gloopy-grpc.lisp). Turns the DAW into a
scriptable REPL target: build tracks, sequence notes, wire effects, drive the
transport, and bounce to WAV, all from Python.

## Install

```sh
cd python
pip install .                 # runtime: grpcio, protobuf
# or, for a dev checkout without installing:
pip install grpcio protobuf
```

The generated stubs (`gloopy/gloopy_pb2*.py`) are committed. If you edit
`proto/gloopy.proto`, regenerate them:

```sh
pip install grpcio-tools
./gen.sh
```

## Use

Start Gloopy (it opens gRPC on `127.0.0.1:50051`), then:

```python
from gloopy import Gloopy, note

with Gloopy() as g:                         # 127.0.0.1:50051
    g.set_tempo(128)
    g.set_swing(0.62)                       # 0.5 straight … 0.75 triplet feel
    lead = g.add_synth_track("lead", wave="SAW", release=0.25)
    g.add_effect(0, "REVERB")               # insert 0 = master
    g.add_clip(lead, notes=[note(60, 0, 1), note(64, 1, 1), note(67, 2, 2)])
    g.play()
    print(g.transport())                    # {'playing': True, 'bpm': 128.0, ...}
    g.render("/tmp/out.wav", tail_seconds=1.0, start_beat=0, end_beat=4)
```

Stream transport/meters/structural-change events:

```python
for ev in g.subscribe(transport=True, changes=True):
    print(ev)                               # {'kind': 'transport', 'position_beats': ...}
```

See [`example.py`](example.py) for a runnable arpeggio-and-render demo. The full
surface (every method maps 1:1 to an RPC in `proto/gloopy.proto`) is in
[`gloopy/client.py`](gloopy/client.py).
