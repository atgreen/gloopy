# Route to a bus and set up a shared reverb send

**Task:** send several tracks to *one* reverb (a classic aux-send setup), and/or
fold tracks into a **submix** you can control with a single fader. Assumes you're
comfortable in the [mixer](effects-and-mixing.md).

Both start from a **bus** — a mixer strip other strips route into. There are two
ways to feed one, and they do different jobs.

## Send vs. output — pick the right one

| | **Send** | **Output** |
|---|---|---|
| Sends… | a **parallel copy** of the signal | the strip's **whole** signal |
| The dry track… | keeps playing to the master | now flows *through* the bus instead |
| Use for… | a shared reverb/delay (aux) | a **submix / group** (all drums on one fader) |

## A shared reverb (send)

1. Right-click a strip's **name** → **New bus…** to create a bus.
2. On the bus strip, add a **Reverb** (its **FX** button → Add effect). Turn its
   **Wet** up — a send bus is usually all-wet.
3. On each track you want reverberated: right-click its name → **Send to ⟨bus⟩** →
   pick a level (25 / 50 / 75 / 100 %) and **Post-fader** (so the send follows the
   track's volume).
4. Now several dry tracks share one reverb. Ride the **bus fader** to set the
   overall reverb amount.

!!! tip "Pre- vs. post-fader"
    **Post-fader** (the usual choice) scales the send with the track's fader, so
    turning the track down turns its reverb down too. **Pre-fader** sends a fixed
    amount regardless of the fader — useful for a fully independent effect.

## A submix / group (output)

To put, say, all your drums under one fader:

- **Fastest:** select the drum strips (click a name, **Shift/Ctrl-click** more)
  and press **Ctrl+G** — Gloopy makes a group bus and routes them into it.
  **Ctrl+Shift+G** ungroups.
- **By hand:** make a **New bus…**, then on each drum strip choose **Output ▸**
  ⟨that bus⟩. Their signal now flows through the bus (not straight to master), so
  the bus fader controls the whole kit and you can put a compressor on it.

A grouped strip's cue reads **GROUP** (fed by outputs) or **RETURN** (fed by
sends) under its name. **Ctrl+Shift+F** folds/unfolds all groups.

## See also

- [Mixer track (bus, send)](../../control-scripting/concepts/model.md#mixer-track-bus-send) —
  the routing model, and the load-bearing rule that a bus must sit after
  everything feeding it.
- [Add insert effects and balance a mix](effects-and-mixing.md).
