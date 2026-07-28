# Arrange with the Session view

**Task:** use the clip-launch grid to try ideas and perform, then move the good
parts into the song. For *why* the two views exist, read
[Arrangement vs. Session](../explanation/arrangement-vs-session.md) first.

Press **Tab** to cycle **Arrange → Session → Mixer**.

## The grid

Columns are **tracks**; rows are **scenes**. Each cell is a **slot** that's either
empty or holds a clip you launch live. The **scene / master column is pinned on
the left** and stays put as you scroll.

## Launch and stop

- **Click a clip cell** to launch it — playback starts on the next musical
  boundary (see quantize, below).
- **Click an empty cell** to stop that track.
- Click a **scene button** (left column) to launch a whole row at once.
- **■ Stop** (top of the left column) stops everything.

A launched clip **overrides that track's arrangement playback** until you stop it
or return to the arrangement.

## Set the launch quantize

The **Q ⟨value⟩** button at the top of the left column sets *when* launches fire —
click it to cycle **Off / 1/16 / 1/8 / 1/4 / 1/2 / 1 bar / 2 bars**. Bigger values
keep everything locked to the grid; **Off** launches instantly. This setting is
saved with the project.

## Build a slot

Right-click a cell for the slot menu:

- **New empty clip** — start a clip to draw into.
- **Copy selected clip here** — drop the currently selected arrangement clip in.
- **Edit clip** (or **double-click** the cell) — open it in the piano roll.
- **Colour ▸** — tint the slot for organisation; **Clear** empties it.

Add rows with **+ Scene** at the bottom of the left column.

## Record into a slot

1. **Arm** the track (the **●** button on its column strip).
2. **Click an empty cell** — it blinks red and records your live input straight
   into that slot.

## Move a performance into the song

Two ways to get session ideas onto the timeline:

- **Capture the whole jam:** in the Session view, press the toolbar **Record**.
  Gloopy writes the clips you launch onto the arrangement timeline as you perform
  them.
- **One clip at a time:** a cell's right-click **Copy to arrangement**.

This matters because **only the arrangement renders** — a great session take isn't
in the bounce until it's on the timeline. →
[Arrangement vs. Session](../explanation/arrangement-vs-session.md#why-renders-come-only-from-the-arrangement).

## See also

- [Scene and session slot](../../control-scripting/concepts/model.md#scene-and-session-slot) —
  the model behind the grid.
- Launch clips and scenes from a script → the session control API in the
  [gRPC reference](../../control-scripting/reference/grpc/index.md).
