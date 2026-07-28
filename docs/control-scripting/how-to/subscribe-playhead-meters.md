# Subscribe to the playhead and meters

**Goal:** get a live push feed of where the transport is and how loud the master
bus is — for a meter bridge, a progress display, a sync tool, or just to watch a
render. This is the streaming half of the [gRPC lane](../concepts/model.md#the-two-control-lanes):
Gloopy pushes `Event`s to you rather than you polling.

Every subscription is one `Subscribe` call that opens a server stream. You choose
which event kinds you want and how often position updates arrive (`interval_ms`);
the stream runs until you stop reading it.

## Event kinds

| Kind | Fields | When |
|------|--------|------|
| `transport` | `playing`, `bpm`, `position_beats` | Every `interval_ms` while subscribed |
| `meters` | `peak_l`, `peak_r` (per-channel), `clipped` | Every `interval_ms` |
| `change` | `kind`, `track_id`, `insert` | When the structure of the project changes |

Ask only for what you need — a meter bridge wants `meters`; a "reload when
someone else edits" tool wants `change`.

## Python

`subscribe` is a generator that yields one `dict` per event until you stop
iterating. Bound it yourself — here with `itertools.islice`:

```python
import itertools
from gloopy import Gloopy

with Gloopy() as g:
    g.play()
    for event in itertools.islice(g.subscribe(transport=True, meters=True), 20):
        if event["kind"] == "transport":
            print(f"beat {event['position_beats']:.2f}")
        elif event["kind"] == "meters":
            print(f"peak L/R {event['peak_l']} {event['peak_r']}")
    g.stop()
```

`interval_ms` (default 100) controls how often `transport`/`meters` events fire.
To watch a whole render, iterate without `islice` and `break` when
`position_beats` passes the end of your song.

## Common Lisp

`subscribe` collects events for roughly `:seconds` seconds and returns them as a
list of plists; pass `:on-event` to react to each as it streams in:

```lisp
(in-package :gloopy)
(play)
(subscribe :transport t :meters t :seconds 3
           :on-event (lambda (ev)
                       (case (getf ev :kind)
                         (:transport (format t "~&beat ~,2f~%" (getf ev :position-beats)))
                         (:meters    (format t "~&peak ~a ~a~%"
                                             (getf ev :peak-l) (getf ev :peak-r))))))
(stop)
```

`:interval-ms` (default 200) sets the update rate. The full return value is the
list of collected event plists, so you can also just bind it and inspect it at
the REPL:

```lisp
(length (subscribe :seconds 1))    ; => how many events arrived in a second
```

See the [Common Lisp client reference](../reference/lisp/index.md#events-server-stream).

## Raw gRPC (grpcurl)

No client library needed — the stream is a normal server-streaming RPC:

```sh
grpcurl -plaintext -proto proto/gloopy.proto -import-path proto \
    -d '{"transport": true, "meters": true, "interval_ms": 200}' \
    127.0.0.1:50051 gloopy.v1.Gloopy/Subscribe
```

Each streamed message is one `Event`; `grpcurl` prints them as JSON as they
arrive. Stop with Ctrl-C.

## Notes

- **Reads are gRPC, not OSC.** The live OSC lane is send-only; to *read* transport
  or meters you subscribe over gRPC. (The Lisp OSC package is
  [encode-only](../reference/lisp/index.md#live-gloopyosc-glosc-osc) for the
  same reason.)
- **Rendering doesn't need a subscription.** An offline
  [render](../reference/lisp/index.md#project-render) is deterministic and
  blocks until done — subscribe only when you want to *watch* live playback.
