# OSC address space

The live, low-latency performance lane. Send OSC over **UDP to port `9000`**;
every address lives under the root **`/gloopy`**. Use it to play notes, turn
knobs, and start/stop in real time. To *build or edit* a composition, use the
[gRPC API](../grpc/index.md) instead.

!!! info "Summary — `Source/OscControl.h` is authoritative"
    OSC has no schema ecosystem, so the address space is defined in Gloopy's OSC
    server itself. The table below is **maintained by hand** against
    [`Source/OscControl.h`](https://github.com/atgreen/gloopy/blob/main/Source/OscControl.h) —
    if the two ever disagree, the source wins. A planned `osc.yaml` schema will
    generate this table, the server's dispatch/validation, and client-side
    argument checking from one definition; until then, this is a summary.

## Conventions

- **Argument types:** `i` = int32, `f` = float32, `s` = string. Ints and floats
  are coerced where sensible.
- **`<id>`** is a track's stable id — the one [gRPC](../grpc/index.md) prints when
  a track is created or a project loads. OSC **acts on** ids; it never creates
  them.
- Messages are **fire-and-forget** and handled on the real-time network thread.
  There's no reply — query state over [gRPC](../grpc/index.md).

## Transport

| Address | Args | Effect |
|---------|------|--------|
| `/gloopy/transport/play` | — | Start playback |
| `/gloopy/transport/stop` | — | Stop and rewind |
| `/gloopy/transport/tempo` | `bpm:f` | Set tempo |
| `/gloopy/transport/seek` | `beats:f` | Move the playhead |
| `/gloopy/transport/panic` | — | All notes off |

## Per-track (live performance)

| Address | Args | Effect |
|---------|------|--------|
| `/gloopy/track/<id>/note` | `pitch:i vel:i` | `vel > 0` = note on, `vel = 0` = note off (pitch 0–127) |
| `/gloopy/track/<id>/cc` | `cc:i val:f` | MIDI CC; `val` 0.0–1.0 maps to 0–127 |
| `/gloopy/track/<id>/vol` | `v:f` | Track volume, 0.0–1.0 |
| `/gloopy/track/<id>/pan` | `p:f` | Pan, −1.0–1.0 |
| `/gloopy/track/<id>/mute` | `m:i` | 0 or 1 |
| `/gloopy/track/<id>/synth` | `name:s value:f` | Tweak a built-in synth parameter live |

## Per-insert (mixer)

| Address | Args | Effect |
|---------|------|--------|
| `/gloopy/insert/<n>/vol` | `v:f` | Insert `n` fader |
| `/gloopy/insert/<n>/pan` | `p:f` | Insert `n` pan |
| `/gloopy/insert/<n>/mute` | `m:i` | 0 or 1 |
| `/gloopy/insert/<n>/fx/<m>/param/<name>` | `v:f` | Effect knob: insert `n`, effect slot `m`, parameter `name` |

## Security

The OSC port is **unauthenticated and intended for localhost / trusted LANs**
only — anyone who can reach the port can control playback. The gRPC port is bound
to `127.0.0.1`. Don't expose either to an untrusted network.

*Related:* live-note fundamentals are in
[the two control lanes](../../concepts/model.md#the-two-control-lanes).
