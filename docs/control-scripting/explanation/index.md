# Explanation

Why the control API is shaped the way it is. This is discussion, not instructions
— for the definitions, see [The Gloopy model](../concepts/model.md); for tasks,
the [how-to guides](../how-to/index.md).

## Why two protocols instead of MIDI-only

A DAW that only spoke MIDI would be limited to notes and 7-bit controllers, with
no way to *build* a song from outside — no "add a track", "load this clip",
"render to WAV", or "give me the current state". Gloopy needs both **structural
control** and **live performance**, and those have opposite requirements:

- **Structural control** wants reliability and richness: create a track and get
  its id back, edit a clip, query the whole model, stream state changes. That's a
  request/response API with a real schema — **gRPC**.
- **Live performance** wants the lowest possible latency and doesn't care about
  acknowledgement: a note-on has to happen *now*, and if one knob-move packet is
  lost, the next one is milliseconds away. That's fire-and-forget **OSC** over
  UDP.

Trying to force both down one protocol makes one of them bad. So Gloopy runs two,
each doing what it's best at.

## Scheduled vs. live

The cleanest way to hold the split in your head:

- **gRPC changes the score.** It edits the composition — the thing that gets
  saved and rendered. Its effects are part of the deterministic arrangement.
- **OSC performs the score.** It plays notes and moves controls in the moment.
  Its effects are live and are **not** on the offline-render path, so renders
  stay reproducible.

## Shared identity

The two lanes aren't independent — they meet at the **track `id`**. gRPC *creates*
ids (when you add a track); OSC *acts on* them (to play that track). A typical
integration creates its tracks over gRPC once, remembers the ids, then performs
over OSC for the rest of the session. This is why ids are stable and printed on
creation.

## Threading, briefly

The interfaces map onto Gloopy's internal lanes: OSC handlers run on the network
thread and touch only real-time-safe state (a per-track MIDI collector, atomics,
a try-lock for effect knobs); gRPC calls perform structural edits on the message
thread. Keeping performance data off the structural path is what lets live play
stay glitch-free while the model is being edited.

## Where this came from

The original design write-up — the scheduled/live model, the three threading
lanes, and the security posture — is in `docs/CONTROL-API.md`. The reference
tables ([gRPC](../reference/grpc/index.md), [OSC](../reference/osc/index.md)) are
generated from the actual sources of truth so they can't drift from it.
