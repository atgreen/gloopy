# gRPC API

The structural, scheduled control lane: build and edit compositions, query state,
and render. Service **`gloopy.v1.Gloopy`** on **`127.0.0.1:50051`**.

!!! info "Summary — the proto is authoritative"
    This page is a **hand-maintained overview**, not the full RPC/message
    reference. For exact request/response shapes, field names, and comments, read
    [`proto/gloopy.proto`](https://github.com/atgreen/gloopy/blob/main/proto/gloopy.proto)
    directly — it is the contract. A generated reference (from the proto comments,
    via `buf generate` + `protoc-gen-doc`, rebuilt in CI) is planned to replace
    this summary; until then, if this page and the proto ever disagree, **the
    proto wins**.

## The proto is the contract

`proto/gloopy.proto` is the single source of truth. Third-party scripts and
hardware controllers pin to it, so it's checked with `buf lint` and
`buf breaking` in CI — a breaking change can't merge without notice.

## Poke it now

Every request/response is described in the proto; you can call it with `grpcurl`:

```sh
grpcurl -plaintext -proto proto/gloopy.proto -import-path proto \
    127.0.0.1:50051 gloopy.v1.Gloopy/GetState
```

## What's in the service

Roughly 140 RPCs, grouped:

- **Transport** — `Play`, `Stop`, `Panic`, `SetTempo`, `SetSwing`, `Seek`,
  `SetLoop`, `GetTransport`.
- **Tempo & time** — tempo markers, `SetTimeSignature`, and beat ↔ seconds ↔
  bar·beat conversions.
- **Tracks** — `AddSynthTrack`, `AddSamplerTrack`, `AddSfzTrack`, `AddSurgeTrack`,
  `AddPluginTrack`, `AddAudioTrack`, plus rename/move/duplicate/colour/remove.
- **Clips & notes** — add/move/split/duplicate/repeat/reverse/crop/gain/normalize/
  bounce clips; `GetClipNotes`, `ImportNotesJSON`, `ExportNotesJSON`.
- **Destructive note ops** — quantize, transpose, humanize, chord, strum, legato,
  echo, invert, ratchet, harmonize, swing, arpeggiate, and more.
- **Recording** — arm, punch, start/stop, take promote/cleanup/recover, input
  enumeration.
- **Mixer, effects, buses, sends** — inserts, effect add/remove/param/bypass,
  `AddBus`, `SetSend`, control groups (VCA-lite), mixer scenes.
- **Automation, modulation, parameters** — automation lanes, LFO/mod routes, and
  the universal `ListParameters`/`Get`/`Set`/`SetParameterNormalized`.
- **Musical model** — scales, tuning, `.scl` import, snap-to-scale; locations
  (markers/ranges/sections/loop/punch/export/skip).
- **Project & IO** — new/load/save, templates, `SaveComposition`,
  `RenderToFile`, export MIDI/stems/track, import MIDI/audio, `AnalyzeFile`,
  `GetWaveform`, `GetState`, `GetDiagnostics`, `Undo`/`Redo`.
- **Events** — `Subscribe` returns a **stream** of `TransportState`, `Meters`,
  and `ChangeEvent` messages.

See [the model](../../concepts/model.md) for what each noun means.
