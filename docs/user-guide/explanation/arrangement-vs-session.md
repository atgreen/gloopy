# Arrangement vs. Session

Gloopy shows the *same* music two ways. Understanding what each view is for — and
what they share — is the key to moving fluidly between sketching an idea and
committing it to a finished song. Press **Tab** to cycle **Arrange → Session →
Mixer**.

## The same clips, two layouts

Both views work with the one thing Gloopy is built around: **clips** — blocks of
notes or audio that [own their content](../../control-scripting/concepts/model.md#clip).
Neither view is a copy of the other's data; they're two arrangements of clips on
the same set of [tracks](../../control-scripting/concepts/model.md#track).

- **Arrangement** is the **timeline**. Clips sit at fixed positions along a bar
  ruler, left to right, and the playhead sweeps through them once. This is the
  song as it will be heard start to finish — and it's the *only* thing that gets
  [rendered](../../control-scripting/concepts/model.md#export-profile).
- **Session** is a **grid**. Columns are tracks; rows are
  [scenes](../../control-scripting/concepts/model.md#scene-and-session-slot). Each
  cell holds a clip you launch *live*, on a musical boundary, in any order you
  like. Nothing here has a fixed position in time — it plays when you fire it.

## When each earns its place

**Reach for the Session view to explore.** Loops, variations, and "what if the
chorus drums came in here?" belong in the grid: launch clips, stack a whole scene,
try an order, change your mind — all without disturbing a timeline. It's a
performance and sketching surface.

**Reach for the Arrangement to commit.** Once you know how the song goes, you lay
clips out on the timeline in the order and at the lengths you want. This is where
a song becomes fixed, editable in detail, and reproducible.

The two aren't walled off. You can **capture** a session performance into the
arrangement, and copy an arrangement clip into a session slot — so an idea found
by launching can become part of the song, and vice versa.

## Why renders come only from the arrangement

Session launching is **live**: which clip is playing depends on what you fired and
when. That's the opposite of what you want from a render, which must be
**deterministic** — the same file every time, whether you bounce it now or in CI
next week.

So Gloopy draws a firm line: **the offline render always comes from the
arrangement**, never from live session playback. A launched session clip overrides
its track's arrangement playback *while you're performing*, but it is not on the
render path. Lay the part into the arrangement and it will render; leave it in the
grid and it stays a live-only idea. (See
[the two control lanes](../../control-scripting/concepts/model.md#the-two-control-lanes)
for the same live-vs-structural split on the scripting side — it's the same
principle, one layer down.)

## One transport, one mixer

Whatever you're doing, there is exactly **one**
[transport](../../control-scripting/concepts/model.md#transport) — one tempo, one
playhead, one clock — and one [mixer](../../control-scripting/concepts/model.md#mixer-track-bus-send).
Switching views never switches songs; it only switches how you're looking at the
one you have. A fader move, an effect, a mute applies the same in both, because
there is only one signal path underneath.

## See also

- [Scene and session slot](../../control-scripting/concepts/model.md#scene-and-session-slot) —
  the precise definitions this essay builds on.
- [Your first track](../tutorials/first-track.md) — build something in the
  arrangement, hands-on.
