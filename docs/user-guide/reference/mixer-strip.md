# Mixer strip controls

Open the mixer with the toolbar **Mixer** button (or **Tab**). Each track has a
strip; **strip 0 is the master**. Buses and groups appear as their own strips.

## On the strip

| Control | What it does |
|---------|--------------|
| **Name** | The strip's name (a track strip shows the track's name + colour). Click to **select** it (Shift/Ctrl-click extends) |
| **Routing cue** | Under the name: **GROUP** (fed by outputs), **RETURN** (fed by sends), **BUS**, or **→ target** |
| **Fader** | Level, 0–1 (vertical) |
| **Pan** | −1…+1 (horizontal) |
| **M** / **S** | Mute / solo |
| **Meter** | Stereo peak meter with a **clip LED** — click the LED to reset it |
| **FX** | Add and edit effects ([menu below](#the-fx-menu)) |

## The FX menu

The **FX** button opens:

- **Add effect** — a built-in (Gain, Filter, Delay, Reverb, Limiter, Bitcrusher,
  Compressor, EQ, Waveshaper, Stereo Widener, Tremolo, Chorus, Flanger, Phaser,
  Auto-pan, Noise Gate, Auto-wah, Ring Mod, and the Scope / Spectrum / Vectorscope
  analyzers).
- **Add plugin** — a scanned VST3/LV2 effect.
- **Edit: ⟨effect⟩** — per existing effect; the bottom editor shows its knobs plus
  **Bypass**, **Remove**, and **Plugin UI**.

Recipe: [Add insert effects and balance a mix](../how-to/effects-and-mixing.md).

## Right-click a fader/pan/knob — the parameter menu

| Item | What it does |
|------|--------------|
| **Automate at playhead** | Drop an automation keyframe (current value @ playhead) |
| **Automation curve ▸** | Linear / Ease in / Ease out |
| **Stepped automation** | Hold values instead of interpolating |
| **Clear automation** | Remove this parameter's lane |
| **Add LFO…** | Modulate the parameter with an LFO (rate, depth, shape, sync…) |
| **MIDI Learn** / **Remove mapping** | Bind or unbind a hardware knob / CC |

Recipe: [Automate a parameter](../how-to/automate-a-parameter.md).

## Right-click a strip name — routing and groups

| Item | What it does |
|------|--------------|
| **New bus…** | Create a bus (a submix / send destination) |
| **Send to ⟨bus⟩ ▸** | Aux send — Off / 25 / 50 / 75 / 100 %, Pre- or Post-fader |
| **Output ▸** | Route the whole strip to Master or a later bus (a submix) |
| **Group N selected strips** (Ctrl+G) | Group the selection into a bus |
| **Ungroup** (Ctrl+Shift+G) | Undo a group |
| **Control group** items | VCA-lite: New group…, Assign to, Group gain, Mute/Solo group, Delete group |
| **Rename strip…** | Rename the insert |
| **Open device window** | Pop the effect chain into a floating window |

Recipe: [Route to a bus and set up a shared reverb send](../how-to/bus-and-reverb-send.md).
**Ctrl+Shift+F** folds/unfolds all groups. See
[Mixer track (bus, send)](../../control-scripting/concepts/model.md#mixer-track-bus-send).
