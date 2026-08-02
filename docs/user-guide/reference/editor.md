# Step editor and piano roll

The **editor** is the bottom panel. Click a clip to load it, then work in one of
two views of the *same* notes. (Keyboard shortcuts are in
[Keyboard shortcuts](keyboard-shortcuts.md#piano-roll-the-editor).)

## Header controls

| Control | What it does |
|---------|--------------|
| **STEPS** / **PIANO** | Switch between the step grid and the piano roll |
| **DEVICES** | Swap the pane to the selected track's effect chain |
| **RACK** | Swap the pane to the selected track's macro rack (perceptual encoders) |
| **AUDITION** | Play notes through the instrument as you click/brush them (default on) |
| **SCALE** | Snap drawn and moved notes to the project scale |
| **KNIFE** | Click a note to split it at that beat |
| **ARP** | Arpeggiate a chord in the roll — Up / Down / Up-Down |
| **LEGATO** | Stretch each note to the next onset (Shift-click = half) |
| **STRUM** | Fan out a chord's voices in time (Shift-click = up-strum) |
| **Chord** | Draw single notes (**Note**) or stamp a chord — maj, min, 7, maj7, min7, sus2, sus4, dim, aug, add9, 6, 9 |

## Piano roll

| Gesture | Effect |
|---------|--------|
| Click empty | Place a 1-beat note |
| Drag body | Move a note |
| Drag right edge | Resize |
| Right-click / double-click | Delete a note |
| Shift-drag | Marquee-select |
| Drag in the **velocity strip** (bottom) | Set a note's velocity; Shift-drag = a velocity ramp |
| Mouse-wheel / Ctrl-wheel | Scroll / zoom vertically |

The keyboard shortcuts (Q quantize, H humanize, S strum, L legato, ↑/↓ transpose,
Delete) act on the selection if there is one, else all notes — see
[Keyboard shortcuts](keyboard-shortcuts.md#piano-roll-the-editor).

## Step grid

A single-lane 16-step-style grid, quickest for drums and simple patterns. Click a
cell to toggle a note at the track's default pitch; the number of steps follows
the clip's content length (up to 64). Both views edit the same clip — see
[Clip](../../control-scripting/concepts/model.md#clip).

## Devices tab

**DEVICES** shows the selected track's effect chain inline: a **◀ Clip** back
button, **+ Device** to add an effect, a row of device buttons (click to select),
per-parameter **rotary knobs**, and **Bypass** / **×** (remove) for the selected
device. The analyzer effects — **Scope**, **Spectrum**, **Vectorscope** — draw a
live display instead of knobs. The same chain is editable in the full
[Mixer](mixer-strip.md).

## Rack tab

**RACK** shows the selected track's **macros** — perceptual encoders that each drive
one or more of the track's synth/effect parameters at once. **+ Macro** adds one;
turn a knob (0..1) and every parameter it drives sweeps across its safe range, so a
single knob moves a whole sound. Under each knob is the macro's name and how many
parameters it drives, and the knobs update live when a macro changes elsewhere (for
example a script `randomize-macros`). **Randomize** rolls every macro to a fresh value
at once — musical, since each mapping stays inside its safe range.

The **⋯** button on each knob opens its menu:

| Item | What it does |
|------|--------------|
| **Rename…** | Give the macro a perceptual name — "Brightness", "Drive" |
| **Map to synth ▸** | Map the macro onto a built-in synth parameter (over that parameter's range) |
| **Map to effect ▸** | Map it onto a parameter of one of the track's insert effects |
| **MIDI Learn** | Arm learn, then move a hardware knob/fader — that CC now drives the macro |
| **Automate at playhead** | Drop an automation keyframe for the macro at the current position — move the playhead and repeat to draw an envelope; playback morphs the macro smoothly between points |
| **Clear automation** | Remove the macro's automation lane |
| **Clear mappings** | Remove all of this macro's mappings (keep the macro) |
| **Remove macro** | Delete the macro |

Map a macro to several parameters and one knob morphs them together. Mapping,
renaming, removing, and Randomize are scriptable from the
[Python](../../control-scripting/reference/python/index.md) and
[Lisp](../../control-scripting/reference/lisp/index.md#macros-the-rack-layer)
clients, where you can also set a narrower `[lo, hi]` safe range per mapping.

**Snapshots** (variations). The strip under the header saves whole knob states:
**+ Snapshot** captures the current positions as a slot; click a slot to recall it
(the knobs animate back to it and the slot highlights until you move a knob).
Right-click a slot to **Morph to this over** ½–8 beats (the whole rack glides smoothly
from where it is to the snapshot), **Recall instantly**, **Insert at playhead**, **Overwrite**
it with the current knobs, **Rename**, or **Delete** it. Snapshots are saved with the project.

**Insert at playhead** stamps the snapshot into the timeline: it writes an automation
breakpoint into *every* macro's lane at the playhead using that snapshot's values. Insert
one snapshot at the start and another later, and playback **morphs the whole rack between
them** as it ramps from one set of points to the next — the snapshot becomes ordinary,
editable automation. (To drive individual macros instead, use **⋯ → Automate at playhead**.)
