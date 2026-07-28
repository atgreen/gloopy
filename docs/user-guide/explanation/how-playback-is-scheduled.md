# How playback is scheduled

What actually happens when you press Play — how clips, loops, and the playhead turn
a static arrangement into sound over time.

## One transport, one clock

There is exactly one [transport](../../control-scripting/concepts/model.md#transport):
one tempo, one playhead, one clock the whole song follows. The playhead's position
is measured in [beats](the-musical-model.md) (a beat is a quarter note). Everything
below is that clock deciding what to sound *now*.

## Playback happens block by block

The audio engine doesn't play the song note by note — it works in small **blocks**
(a few hundred samples at a time). For each block, it asks a simple question: *which
notes and audio fall between where the playhead is and where it'll be at the end of
this block?* Anything in that window is emitted at the right sample offset inside
the block.

This is why timing is tight and why the whole thing scales: the engine only ever
looks at the sliver of time it's about to play, not the entire song.

## Clips are anchored, and they loop

A [clip](../../control-scripting/concepts/model.md#clip) sits at a fixed position on
its track's timeline (`startBeat`). Its **content** has a length, and by default a
clip **loops** to fill however long it's placed:

- Drop a one-bar idea across a four-bar span and it repeats four times. You don't
  copy it — the clip's content just wraps every bar as the playhead crosses it.
- Turn looping off and the content plays once, then the clip is silent for the rest
  of its span.

So a clip's *placement* (where and how long) is separate from its *content* (the
notes/audio inside), and looping bridges the two.

## The loop region wraps the playhead

That's a *clip* looping. Separately, the **transport loop** — the region you set
with the **Loop** button — wraps the *playhead itself*: when it reaches the end of
the loop, it jumps back to the start and keeps going. Everything inside plays again
because the clock literally went back in time. This is what lets you record pass
after pass over the same bars (see
[loop-record and takes](../how-to/record-midi-and-audio.md#loop-record-and-stack-takes)).

## Session clips play alongside, live

A launched [session](arrangement-vs-session.md) clip is scheduled differently: it
has no timeline position, so it **loops from the moment you launched it**, in its
own running beat count. While it plays it **overrides that track's arrangement
playback**; stop it and the arrangement takes back over.

Session launching is a **live** decision — it depends on what you fired and when —
which is exactly why it's kept off the render path (below).

## Automation and modulation move per block

Time-varying values ride the same clock. Each block, the engine reads any
[automation](../../control-scripting/concepts/model.md#automation) lane and
[modulation](../../control-scripting/concepts/model.md#modulation) LFO at the current
playhead position and applies the value for that block. So a drawn fade or an LFO
wobble is evaluated continuously as the playhead moves — no separate pass.

## Offline render is the same clock, deterministic

A [render](../how-to/export-audio.md) runs this exact block-by-block scheduling, but
**freewheeling** — as fast as the CPU allows instead of in real time — over the
**arrangement**. Because it reads the same fixed clips, tempo map, and automation,
it produces the **same file every time**: no live session launches, no real-time
jitter, no dropouts. That determinism is the reason renders come only from the
arrangement, never from live session playback — see
[Arrangement vs. Session](arrangement-vs-session.md#why-renders-come-only-from-the-arrangement).

## See also

- [Transport](../../control-scripting/concepts/model.md#transport),
  [Clip](../../control-scripting/concepts/model.md#clip), and
  [Scene and session slot](../../control-scripting/concepts/model.md#scene-and-session-slot) —
  the definitions.
- [The musical model](the-musical-model.md) — the beat grid this clock runs on.
