# Common Lisp client

The Lisp client lives in [`common-lisp/`](../../concepts/model.md) as the ASDF
system `gloopy` (dependencies vendored via ocicl). It mirrors the two
[control lanes](../../concepts/model.md#the-two-control-lanes) as two packages.

!!! info "Generated (planned)"
    The full symbol reference is generated from the `:documentation` strings and
    export lists in `common-lisp/src/` by 40ants-doc, and rebuilt in CI. *(The
    generator wiring is pending; the summary below is maintained by hand until
    then.)*

## Two packages, two lanes

| Package | Nickname | Lane | Transport |
|---------|----------|------|-----------|
| `gloopy` | — | Structural (gRPC) | `127.0.0.1:50051` |
| `gloopy.osc` | `glosc` | Live (OSC) | UDP `9000` |

## Structural — `gloopy` (gRPC)

```lisp
(asdf:load-system :gloopy)
(in-package :gloopy)
(connect)                                     ; 127.0.0.1:50051
(let ((id (add-synth-track "Lead" :wave :saw)))
  (add-clip id :notes (list (note 60 0 1) (note 64 1 1) (note 67 2 1)))
  (play)
  (subscribe :seconds 3 :on-event #'print)    ; stream playhead + meters
  (stop)
  (render "/tmp/mix.wav"))                     ; offline bounce
```

Exposes connection (`connect`, `disconnect`, `connectedp`), transport, tracks,
clips, mixer/effects, plugins, project/render, and `subscribe`. Query calls
return **plists**.

## Live — `gloopy.osc` / `glosc` (OSC)

```lisp
(glosc:with-osc ()
  (glosc:note-on 0 60 100)
  (glosc:cc 0 74 0.8)
  (glosc:vol 0 0.9)
  (glosc:play))
```

Exposes live MIDI (`note-on`, `note-off`, `chord`, `cc`), params (`vol`, `pan`,
`mute`), `fx-param`, and transport (`play`, `stop`, `tempo`, `seek`).

!!! note "OSC layer is encode-only"
    The Lisp OSC support is used for **encoding** messages onto the wire. See
    `common-lisp/README.md` for the current status and worked examples.
