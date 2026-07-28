# Drive Gloopy from Python

**You will:** connect to a running Gloopy, build a short song from scratch —
tempo, an instrument track, a looping clip — play it, watch the playhead stream
back, and bounce it to a WAV. About 10 minutes.

This is the fastest way to see the [control API](../concepts/model.md#the-two-control-lanes)
in action. We use **gRPC** here — the structural lane that builds and edits a
composition.

## 1. Install the client

```sh
pip install ./python
```

This installs the `gloopy` package (it needs `grpcio` and `protobuf`).

## 2. Start Gloopy

Launch the app normally. On startup it listens on **gRPC `127.0.0.1:50051`** and
**OSC UDP `9000`** — no flags needed. Leave it running.

## 3. Build a song

Create `first_song.py`:

```python
from gloopy import Gloopy, note

with Gloopy() as g:                        # connects to 127.0.0.1:50051
    g.set_tempo(110)

    lead = g.add_synth_track("Lead", wave="SAW")
    g.add_clip(lead, notes=[
        note(60, 0, 1),                    # C  at beat 0, 1 beat long
        note(64, 1, 1),                    # E
        note(67, 2, 1),                    # G
        note(72, 3, 1),                    # C (octave up)
    ])

    g.play()
```

Run it:

```sh
python first_song.py
```

Gloopy plays a four-note arpeggio. `add_synth_track` returned the new track's
**stable `id`** — every later call refers to the track by that id. The `note()`
helper is just `note(pitch, start_beat, length_beats, velocity=0.8)`.

!!! note "Why the clip loops"
    You gave four beats of notes; on a longer arrangement the clip **loops** by
    default. Clips own their content and repeat — see
    [Clip](../concepts/model.md#clip).

## 4. Watch the playhead

The same connection can **subscribe** to a live feed of transport position and
meters:

```python
import itertools

with Gloopy() as g:
    g.play()
    for event in itertools.islice(g.subscribe(transport=True, meters=True), 20):
        print(event)
    g.stop()
```

This is the streaming half of gRPC — Gloopy pushes state to you, rather than you
polling. `subscribe` yields events until you stop reading; here `islice` takes
the first 20. For meters, sync, and CI recipes, see
[Subscribe to the playhead and meters](../how-to/subscribe-playhead-meters.md).

## 5. Bounce to a WAV

Rendering is offline and deterministic — it comes from the arrangement, not from
live playback:

```python
with Gloopy() as g:
    g.render("/tmp/first_song.wav", tail_seconds=1.0)
```

You now have a rendered mix on disk.

## Where to go next

- The full method list → [Python client reference](../reference/python/index.md).
- The same moves, live and low-latency (notes, knob turns) → the
  [OSC address space](../reference/osc/index.md).
- What "structural vs. live" really means → [the two control lanes](../concepts/model.md#the-two-control-lanes).

!!! tip "Prefer Lisp?"
    The [Common Lisp quickstart](lisp-quickstart.md) mirrors this exactly:
    `(connect)`, `(add-synth-track "Lead" :wave :saw)`, `(add-clip …)`, `(play)`,
    `(render "/tmp/mix.wav")`.
