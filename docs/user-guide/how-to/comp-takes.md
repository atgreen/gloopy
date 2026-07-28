# Comp, promote, and clean up takes

**Task:** you loop-recorded a phrase several times; now pick the best pass (or
splice a "comp" from several) and tidy up. Assumes you've
[recorded with Loop on](record-midi-and-audio.md#loop-record-and-stack-takes).

## How takes stack

When you record with **Loop** on, each pass through the loop is kept as a **take**.
They stack at the same spot on the track — the newest is active and the earlier
passes sit behind it as **muted alternates** (a take lane). Nothing is thrown
away until you clean up.

## Choose a take

Right-click a take clip in the arrangement to get the take menu:

- **Use this take** — make *this* pass the active one and mute its siblings. Do
  this per section to **comp**: keep take 2's verse and take 4's chorus by
  activating each where it's best.
- **Promote take (keep)** — lift the chosen take out as a normal clip while
  keeping the alternates around, in case you want to revisit them.

Audition as you go with **Space**; the active take is what plays.

## Clean up

Once you're happy:

- **Clean up unused takes** (take right-click menu) — removes the alternates you
  didn't choose, leaving just the comped result. This is the tidy-up step, so do
  it only when you're sure.

!!! note "Recovering takes"
    Raw recorded passes are kept under the composition's `assets/recordings/`, and
    a **recover-takes** operation can bring cleaned-up passes back — but that one
    is currently reachable through the [control API](../../control-scripting/index.md),
    not a desktop menu. Until you clean up, every pass is still on the track.

## See also

- [Record MIDI and audio](record-midi-and-audio.md) — where takes come from.
- [Take](../../control-scripting/concepts/model.md#take) — the precise definition.
