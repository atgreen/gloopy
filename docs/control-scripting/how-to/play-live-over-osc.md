# Play Gloopy live over OSC

**Goal:** perform in real time — fire notes, sweep a filter, ride a fader, drive
the transport — from a script or a control surface. This is the
[OSC lane](../concepts/model.md#the-two-control-lanes): fire-and-forget messages
over **UDP to port `9000`**, handled on Gloopy's real-time thread with the lowest
possible latency.

OSC **performs**; it does not build. It acts on track and insert **ids that
already exist** — so create and name things over
[gRPC](../reference/grpc/index.md) first, or use the tracks a project loaded with.

## 1. Know your ids

An OSC address targets a track by its stable `id`. Two ways to find them:

- Gloopy **prints them on startup** — e.g. `[osc] tracks: 0=Kick 1=Snare … 5=Pad`.
- Or ask over gRPC: `list_tracks` / `(list-tracks)` returns each track's `:id`.

The examples below use track `5`.

## 2. Perform from Common Lisp

The `gloopy.osc` package (nickname `glosc`) is the OSC client. `with-osc` opens
the UDP socket for the duration of a block; every call is one datagram:

```lisp
(asdf:load-system :gloopy)

(glosc:with-osc ()
  (glosc:tempo 128.0)
  (glosc:play)
  ;; a two-bar A-minor arpeggio, live
  (dolist (n '(57 60 64 69 72 69 64 60))
    (glosc:note-on 5 n 100)
    (sleep 0.22)
    (glosc:note-off 5 n))
  ;; ride a filter and the fader while it plays
  (glosc:cc  5 74 0.8)          ; CC 74 (cutoff), 0..1
  (glosc:vol 5 0.7)             ; track volume
  (glosc:stop))
```

The whole live surface is there: `note-on` / `note-off` / `chord` / `cc`, the
`vol` / `pan` / `mute` params, `fx-param` for effect knobs, and `tempo` / `seek`
/ `play` / `stop`. There's also `(glosc:demo)` — a one-call arpeggio to confirm
you're wired up. See the
[Common Lisp reference](../reference/lisp/index.md#live-gloopyosc-glosc-osc).

!!! tip "Velocity and value ranges"
    `note-on` velocity is **1–127**; `cc` and `vol`/`pan` values are **0.0–1.0**
    floats (Gloopy scales CC to 0–127, and pan spans −1…+1). Sending an integer
    where a float is expected — or vice versa — changes the OSC type tag, so keep
    velocities integers and levels floats.

## 3. Perform from anywhere else

There's nothing Lisp-specific about the wire — any OSC sender works. The Python
`gloopy` client is gRPC-only, so from Python reach for a plain OSC library such as
[python-osc](https://pypi.org/project/python-osc/):

```python
from pythonosc.udp_client import SimpleUDPClient

osc = SimpleUDPClient("127.0.0.1", 9000)
osc.send_message("/gloopy/track/5/note", [60, 100])   # note on: pitch, velocity
osc.send_message("/gloopy/track/5/cc",   [74, 0.8])   # filter cutoff
osc.send_message("/gloopy/transport/play", [])
```

Every address and its argument types are in the
[OSC address space reference](../reference/osc/index.md). A hardware controller or
a tool like an OSC-capable DAW/sequencer can send the same messages.

## 4. Reads happen over gRPC

OSC is **fire-and-forget** — there's no reply, and a packet that meets no listener
never aborts a performance. To *read* where the playhead is or how loud the master
is, [subscribe over gRPC](subscribe-playhead-meters.md). A common shape is to hold
**both** connections: gRPC to build the song and read state, OSC to perform on it.

## 5. Map a knob to any parameter (MIDI-learn)

A `cc` message isn't limited to a synth — via a
[controller map](../concepts/model.md#controller-map) you can bind a CC (or an
`osc:<name>` source) to *any* parameter and scale it. Set the mapping up over
gRPC (or the desktop **Maps** window), then a single `(glosc:cc …)` rides that
target live. That's how a hardware knob ends up controlling a reverb's wet level
or a group fader.

## See also

- [OSC address space](../reference/osc/index.md) — every message, with argument
  types and the localhost-only security note.
- [The two control lanes](../concepts/model.md#the-two-control-lanes) — why live
  performance and structural edits use different protocols.
