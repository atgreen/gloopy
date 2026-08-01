# Drive Gloopy from Common Lisp

**You will:** connect to a running Gloopy from a Lisp REPL, build a short song
from scratch — tempo, an instrument track, a looping clip — play it, watch the
playhead stream back, and bounce it to a WAV. About 10 minutes.

This mirrors the [Python quickstart](python-quickstart.md) move for move, in
Lisp. We use **gRPC** here — the structural lane that builds and edits a
composition (see [the two control lanes](../concepts/model.md#the-two-control-lanes)).

!!! info "Before you start"
    You need a Common Lisp — [SBCL](http://www.sbcl.org/) is what we test with —
    and [ocicl](https://github.com/ocicl/ocicl) to fetch the vendored
    dependencies. Everything below runs from the `common-lisp/` directory of the
    Gloopy checkout.

## 1. Install the client

The client is the ASDF system `gloopy`, in [`common-lisp/`](https://github.com/atgreen/gloopy/tree/main/common-lisp).
Its dependencies — notably [**ag-grpc**](https://github.com/atgreen/ag-grpc)
(the gRPC + protobuf stack) — are vendored with ocicl. From a fresh checkout:

```sh
cd common-lisp
ocicl install
```

That populates `ocicl/` from `ocicl.csv`. You only do this once.

## 2. Start Gloopy

Launch the app normally. On startup it listens on **gRPC `127.0.0.1:50051`** and
**OSC UDP `9000`** — no flags needed. Leave it running.

## 3. Build a song

Start SBCL **from the `common-lisp/` directory** (so ocicl and the current
directory are on the source-registry), load the system, and enter the package:

```lisp
(asdf:load-system :gloopy)
(in-package :gloopy)

(connect)                                     ; 127.0.0.1:50051

(let ((lead (add-synth-track "Lead" :wave :saw)))
  (add-clip lead :notes (list (note 60 0 1)   ; C  at beat 0, 1 beat long
                              (note 64 1 1)   ; E
                              (note 67 2 1)   ; G
                              (note 72 3 1))) ; C (octave up)
  (play))
```

Gloopy plays a four-note arpeggio. `add-synth-track` returned the new track's
**stable `id`** — every later call refers to the track by that id. The `note`
helper is just `(note pitch start-beat length-beats &optional (velocity 0.8))`,
with beats and velocity as its arguments.

!!! tip "Note names, not just numbers"
    `pitch` also takes a name, and `length` a shorthand — `(note "E4" 1 "q")`.
    For a whole melody in one line there's `(mini "c4q e g c5")`, plus `seq`,
    `scale` and `chord`. See [Describing notes](../reference/describing-notes.md).

!!! note "Why the clip loops"
    You gave four beats of notes; on a longer arrangement the clip **loops** by
    default (`:looped t`). Clips own their content and repeat — see
    [Clip](../concepts/model.md#clip).

Queries come back as **plists**, so you can pull the song apart at the REPL:

```lisp
(transport)          ; => (:PLAYING T :BPM 120.0 :POSITION-BEATS 2.0)
(list-tracks)        ; => ((:ID 5 :NAME "Lead" :TYPE 0 :VOLUME 0.8 ...) ...)
(getf (transport) :bpm)
```

## 4. Watch the playhead

The same connection can **subscribe** to a live feed of transport position and
meters. `subscribe` collects events for a few seconds and returns them as a list
of plists; pass `:on-event` to react to each as it arrives:

```lisp
(play)
(subscribe :seconds 3 :on-event #'print)      ; stream playhead + meters
(stop)
```

Each event is a plist tagged by `:kind` — `(:KIND :TRANSPORT :PLAYING T :BPM
120.0 :POSITION-BEATS …)` or `(:KIND :METERS :PEAK-L … :PEAK-R …)`. This is the
streaming half of gRPC: Gloopy pushes state to you, rather than you polling.

## 5. Bounce to a WAV

Rendering is offline and deterministic — it comes from the arrangement, not from
live playback:

```lisp
(render "/tmp/first_song.wav" :tail-seconds 1.0)
```

You now have a rendered mix on disk. When you're done:

```lisp
(disconnect)
```

## Where to go next

- Every function in both packages → [Common Lisp client reference](../reference/lisp/index.md).
- The same moves, live and low-latency (notes, knob turns) → the
  [OSC address space](../reference/osc/index.md) and the `gloopy.osc` package.
- What "structural vs. live" really means → [the two control lanes](../concepts/model.md#the-two-control-lanes).

!!! tip "Prefer Python?"
    The [Python client](../reference/python/index.md) mirrors this exactly:
    `Gloopy()`, `add_synth_track("Lead", wave="SAW")`, `add_clip(…)`, `play()`,
    `render("/tmp/mix.wav")`. See the [Python quickstart](python-quickstart.md).
