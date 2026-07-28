# Automate a parameter

**Task:** make a value change over the course of the song — a fader that rides up
into the chorus, a filter that opens over eight bars. Automation plays back from
the **arrangement**.

## Where automation is authored

Automation is set from the **parameter's own right-click menu** in the mixer.
Right-click a **fader**, a **pan** control, or an **effect knob** to get its menu.
(There's no separate breakpoint-drawing lane to open — you place keyframes on the
parameter itself.)

## Place keyframes at the playhead

The desktop workflow is **keyframe-at-playhead** — set the value where you want
it, and stamp it at the current time:

1. Move the **playhead** to where the change should begin (click the ruler, or
   play and stop).
2. Set the parameter to its starting value (drag the fader/knob).
3. Right-click it → **Automate at playhead**. That drops a keyframe: *this value,
   this beat*.
4. Move the playhead later, change the value, and **Automate at playhead** again.
   Two keyframes make a ramp; add more for a shape.

Play back and the parameter now follows the keyframes you placed.

## Shape the motion

The same right-click menu shapes how keyframes connect:

- **Automation curve ▸** — **Linear**, **Ease in**, or **Ease out** between points.
- **Stepped automation** — hold each value until the next keyframe (a stair-step)
  instead of interpolating.
- **Clear automation** — remove the lane for this parameter and start over.

## Automation vs. an LFO vs. a knob

Automation is for an **exact, drawn-in-time** shape. If you instead want *hands-off
cyclic* movement, or to *perform* the change live, use a different tool from the
same right-click menu:

- **Add LFO…** — a low-frequency oscillator (rate, depth, shape, tempo-sync)
  wobbles the parameter continuously, no keyframes. That's **modulation**.
- **MIDI Learn** — bind a hardware knob or CC to the parameter and play it by
  hand. That's **MIDI-learn**; manage bindings in the **Maps** window.

See [What to use for what](../what-to-use-for-what.md#automation-vs-modulation-vs-midi-learn)
for choosing between the three.

## See also

- [Automation](../../control-scripting/concepts/model.md#automation),
  [Modulation](../../control-scripting/concepts/model.md#modulation),
  [Controller map](../../control-scripting/concepts/model.md#controller-map) — the
  precise models.
- Scripts can author full breakpoint lanes directly via `SetAutomation` — see the
  [gRPC reference](../../control-scripting/reference/grpc/index.md).
