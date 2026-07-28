<!--
SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
SPDX-License-Identifier: AGPL-3.0-only
-->

# gloopy — Common Lisp client for the Gloopy DAW

A Common Lisp library for driving [Gloopy](../README.md) over its two control
lanes (see [`../docs/CONTROL-API.md`](../docs/CONTROL-API.md)):

| Package | Lane | Transport | Role |
|---|---|---|---|
| `gloopy` | gRPC | TCP `127.0.0.1:50051` | **structural** — create/remove tracks, schedule clips, host plugins, add effects, load/save, bounce to WAV, stream playhead + meters |
| `gloopy.osc` (nick `glosc`) | OSC | UDP `127.0.0.1:9000` | **live** — fire-and-forget notes, CC, real-time knob turns, transport |

gRPC **creates and names** things and hands back stable **ids**; OSC
**performs** on those ids with the lowest possible latency.

## Dependencies

Vendored with [ocicl](https://github.com/ocicl/ocicl) (see `ocicl.csv`); the
notable ones:

- [**ag-grpc**](https://github.com/atgreen/ag-grpc) — the gRPC + protobuf stack.
  `../proto/gloopy.proto` is compiled to Lisp at load time into an isolated
  `GLOOPY.PB` package.
- **osc** — a third-party OSC implementation. We use it for the OSC *wire
  format* (message construction + type-tagging); the datagram socket itself is
  driven with **usocket**.

From a fresh checkout, `ocicl install` (run in this directory) fetches them.

## Loading

Run SBCL **from this directory** so ocicl and the cwd source-registry are on the
path, then:

```lisp
(asdf:load-system :gloopy)
```

## Structural lane — `gloopy`

```lisp
(in-package :gloopy)
(connect)                                         ; 127.0.0.1:50051
(let ((id (add-synth-track "Lead" :wave :saw :release 0.3)))
  (add-clip id :notes (list (note 60 0 1) (note 64 1 1)
                            (note 67 2 1) (note 72 3 1)))
  (play)
  (subscribe :seconds 3 :on-event #'print)        ; stream playhead + meters
  (stop)
  (render "/tmp/mix.wav"))                         ; offline bounce
```

Every RPC is a plain function; queries come back as plists:

```lisp
(get-state)   ; => (:transport (...) :tracks (...) :inserts (...))
(list-plugins)
```

## Live lane — `gloopy.osc` / `glosc`

```lisp
(in-package :gloopy.osc)      ; or use the GLOSC nickname
(connect)                     ; 127.0.0.1:9000, UDP
(note-on 5 60 100)            ; middle C on track 5, right now
(chord   5 '(57 60 64))       ; an A-minor chord
(cc      5 74 0.5)            ; filter cutoff via CC 74
(vol     5 0.8)               ; track volume
(fx-param 6 0 "Wet" 0.6)      ; reverb wet on insert 6, effect slot 0
(tempo   128.0)
(disconnect)
```

`with-osc` scopes the connection:

```lisp
(glosc:with-osc ()
  (glosc:note-on 5 60 100)
  (sleep 0.5)
  (glosc:note-off 5 60))
```

UDP is fire-and-forget: `connect` prepares the socket but does not confirm
anyone is listening, and a stray "port unreachable" never aborts a performance.

## Tests

Offline — no running Gloopy required. They check the OSC wire encoding
byte-for-byte against an independent reference and exercise the pure gRPC-side
helpers:

```lisp
(asdf:test-system :gloopy)
```
