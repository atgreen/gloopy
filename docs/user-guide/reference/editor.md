# Step editor and piano roll

The **editor** is the bottom panel. Click a clip to load it, then work in one of
two views of the *same* notes. (Keyboard shortcuts are in
[Keyboard shortcuts](keyboard-shortcuts.md#piano-roll-the-editor).)

## Header controls

| Control | What it does |
|---------|--------------|
| **STEPS** / **PIANO** | Switch between the step grid and the piano roll |
| **DEVICES** | Swap the pane to the selected track's effect chain |
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
