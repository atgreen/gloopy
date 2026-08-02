# Automate a parameter

**Task:** make a value change over the course of the song — a fader that rides up
into the chorus, a filter that opens over eight bars. Automation plays back from
the **arrangement**, and you can see and edit it right on the timeline.

## Automation lanes on the timeline

Every automated parameter draws as a **curve with breakpoints** over its track's row
in the arrangement. To work with it directly:

1. Click the **▾** in a track's header to break its automation out into a **sub-lane**
   below the clips.
2. Click **+ Lane** and pick a parameter (any of the track's synth, macro, or mix
   params). A lane appears with a keyframe at the playhead.
3. Edit the curve directly:
   - **Alt-click** a lane to add a breakpoint.
   - **Drag** a breakpoint to move it (up/down = value, left/right = time, snapped to
     the grid).
   - **Double-click** (or right-click) a breakpoint to delete it.
   - The **×** in a lane's header removes the whole lane.

Stack several lanes under one track for several parameters at once. When a track is
*collapsed*, its automation still draws as a translucent overlay on the clips.

## Shape the motion

Right-click a lane for its menu:

- **Smooth (ramp)** vs **Stepped (hold)** — interpolate between points, or hold each
  value until the next (a stair-step).
- **Curve ▸** — **Linear**, **Ease in**, or **Ease out** between points.
- **Remove lane** — clear this parameter's automation.

## Bus, group, and master automation

Buses, groups, and the **master** don't have clips, so they get their own **content-less
rows below the tracks**, carrying only automation. The master row is always present; a
bus row appears once the bus has automation. A **control group (VCA)** fader is
automatable too (`group/<name>/gain`) — automating it scales all its members without
touching their own automation.

## Keyframe at the playhead (the quick way)

You don't have to open a lane to drop a point. From a **parameter's own right-click menu**
in the mixer (a fader, pan, or effect knob) — or a macro's **⋯** menu in the RACK panel —
choose **Automate at playhead**: it stamps *this value, this beat*. Move the playhead,
change the value, and stamp again. Two keyframes make a ramp.

## Write mode — perform it live

To capture movement instead of drawing it, arm **Write** in the RACK panel. While the
transport plays, your macro moves (including snapshot **morphs**) are recorded as
automation. It's *latch*-style: a parameter you touch stops reading its own automation
so your hands don't fight the playback, and the recording is revealed when you stop.

## Automation vs. an LFO vs. a knob

Automation is for an **exact, drawn-in-time** shape. For *hands-off cyclic* movement or
to *perform* a change live, use a different tool from the same right-click menu:

- **Add LFO…** — a low-frequency oscillator (rate, depth, shape, tempo-sync) wobbles the
  parameter continuously, no keyframes. That's **modulation**.
- **MIDI Learn** — bind a hardware knob or CC to the parameter and play it by hand.
  That's **MIDI-learn**; manage bindings in the **Maps** window.

See [What to use for what](../what-to-use-for-what.md#automation-vs-modulation-vs-midi-learn)
for choosing between the three.

## See also

- [Automation](../../control-scripting/concepts/model.md#automation),
  [Modulation](../../control-scripting/concepts/model.md#modulation),
  [Controller map](../../control-scripting/concepts/model.md#controller-map) — the
  precise models.
- Scripts can author full breakpoint lanes directly via `SetAutomation` — see the
  [gRPC reference](../../control-scripting/reference/grpc/index.md).
