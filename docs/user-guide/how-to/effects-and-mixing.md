# Add insert effects and balance a mix

**Task:** open the mixer, add an effect to a track, and set levels so nothing
clips. Assumes you have a few tracks playing.

## Open the mixer

Click **Mixer** in the toolbar (or press **Tab** to cycle to it). Each track has a
**strip**: a name, a vertical **fader**, a horizontal **pan**, **M** (mute), **S**
(solo), a stereo **peak meter**, and an **FX** button.

## Balance levels

1. Press **Play** and watch the **meters**. If a strip's **clip LED** lights, that
   strip is peaking — pull its fader down (click the LED to reset it).
2. Set relative levels with the **faders**; place instruments left/right with
   **pan**.
3. Use **S** to solo a track and hear it alone, **M** to mute it.

!!! tip "Leave headroom"
    Aim for peaks a few dB below the top of the meter. Summing many tracks at full
    level clips the master — bring individual faders down rather than pushing them
    all up.

## Add an insert effect

1. Click the strip's **FX** button → **Add effect** and pick a built-in (Reverb,
   Delay, EQ, Compressor, Limiter, and more), or **Add plugin** to host a
   VST3/LV2.
2. The bottom of the mixer shows the selected effect's **knobs**, plus **Bypass**,
   **Remove**, and (for plugins) **Plugin UI**.
3. Add several — they process **in order**, top to bottom, forming the strip's
   chain.

You can also edit a track's chain from the editor's **DEVICES** tab (bottom panel)
without opening the full mixer, and **Open device window** (strip name menu) pops
the chain into a floating window so several are visible at once.

## Tame a peaky master

Gloopy doesn't limit by default. If the master meter clips even after balancing,
add a **Limiter** as the last effect on the **master** strip (the leftmost, index
0) — it's a normal insert, not automatic.

## See also

- [Route to a bus and set up a shared reverb send](bus-and-reverb-send.md) — one
  reverb feeding many tracks.
- [Effect](../../control-scripting/concepts/model.md#effect) — the full built-in
  set, including the scope / spectrum / vectorscope analyzers.
- [Add and control effects from a script](../../control-scripting/how-to/effects-from-a-script.md).
