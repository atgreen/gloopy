# Bind a hardware knob to any parameter (MIDI-learn)

**Goal:** drive *any* Gloopy parameter — a fader, a filter cutoff, a reverb's wet
level — from a hardware knob, a MIDI CC, or an OSC source, with your own range.
This is the [controller map](../concepts/model.md#controller-map) (MIDI-learn)
system.

A map connects a **source** to a **target**, scaled to a range:

- **source** — `cc:<n>` (a MIDI CC number), `osc:<name>` (a named OSC source), or
  any string you feed by hand.
- **target** — a [parameter id](../concepts/model.md#parameter-the-universal-parameter-model),
  e.g. `track/5/volume`, `track/5/pan`, or `effect/6/0/Wet`.
- **lo / hi** — the output range the source's 0–1 maps onto. **Set `lo > hi` to
  invert.**

## Bind it explicitly

If you know the CC number and target, map them directly:

```python
from gloopy import Gloopy

with Gloopy() as g:
    # CC 74 drives track 5's filter cutoff, over the top half of its range
    g.add_controller_map("cc:74", "track/5/synth/cutoff", lo=0.5, hi=1.0)
    g.list_controller_maps()          # [{'source': 'cc:74', 'target': ..., 'lo': .5, 'hi': 1.0, ...}]
```

## Or learn it by moving the knob

Don't know the CC number? Arm **learn** on the target, then wiggle the control —
the next source that arrives binds to it:

```python
g.midi_learn("effect/6/0/Wet")   # arm; next controller move binds here ('' cancels)
# now move the hardware knob
```

On the desktop this is the same thing: right-click a mixer fader/knob → **MIDI
Learn**, then move your controller. Manage the resulting bindings in the **Maps**
window (bypass, re-range, invert, remove).

## Feed it (and test without hardware)

A real MIDI CC or an OSC message (`/gloopy/track/5/cc`) feeds the map. You can also
feed a source by hand — handy for testing or for a software controller:

```python
g.set_controller("cc:74", 0.0)    # -> cutoff = 0.5  (lo)
g.set_controller("cc:74", 1.0)    # -> cutoff = 1.0  (hi)
```

## Manage bindings

```python
g.set_controller_bypass("cc:74", "track/5/synth/cutoff", True)   # disable, keep it
g.remove_controller_map("cc:74")                                  # remove
```

!!! tip "MIDI-learn vs. automation vs. modulation"
    Controller maps are for **performing** a parameter live. For a fixed drawn
    shape use [automation](../reference/grpc/index.md); for hands-off cyclic
    movement use [modulation](../concepts/model.md#modulation) (`set_modulation`).
    See [What to use for what](../../user-guide/what-to-use-for-what.md#automation-vs-modulation-vs-midi-learn).

## See also

- [Play Gloopy live over OSC](play-live-over-osc.md) — a `cc` message riding a
  mapped target is how a knob ends up on a reverb.
- [Parameter model](../concepts/model.md#parameter-the-universal-parameter-model) —
  the target id grammar.
