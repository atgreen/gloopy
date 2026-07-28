# What to use for what

Gloopy often gives you two features that sound similar. This page is the quick
decision guide — *when* to reach for each. Every row links to the fuller
explanation.

## Arrange vs. Session

| Use the **Arrangement** when… | Use the **Session** when… |
|---|---|
| You're laying out the song start to finish | You're exploring loops and ideas |
| You want it to **render** (only the arrangement does) | You're performing / launching clips live |
| You need precise, editable positions on a timeline | Order and timing are still up for grabs |

**Rule of thumb:** *Session to find it, Arrangement to commit it.* Press **Tab**
to switch. → [Arrangement vs. Session](explanation/arrangement-vs-session.md).

## Synth vs. Sample vs. SFZ vs. Plugin

These are **not different track types** — they're all
[Instrument tracks](../control-scripting/concepts/model.md#track) that differ only
in their [generator](../control-scripting/concepts/model.md#generator):

| Add… | When you want… |
|---|---|
| **+ Synth** | A built-in oscillator + envelope — fast, tweakable, no files |
| **+ Sample** | To play one `.wav` (a one-shot hit or a loop) |
| **+ SFZ** | A multisampled instrument (a real piano, a drum kit) via the SFZ format |
| **+ Plugin** | A hosted VST3/LV2 — including full synths like Surge XT |

**Rule of thumb:** built-in Synth to sketch, Plugin when you want a specific
instrument's sound and editor.

## Save Project vs. Export Project

| Action | Produces | Use it to… |
|---|---|---|
| **Save Project** (Ctrl+S) | A **composition folder** — readable text, a git repo | Keep working; version with git |
| **Export Project (.gloopy)** | A single-file **zip** of that folder | Hand the whole project to someone as one file |

**Rule of thumb:** Save to *work*; Export to *share*. The folder is the living
project; the `.gloopy` is a snapshot to send. →
[Two forms on disk](../control-scripting/concepts/model.md#on-disk-two-forms),
[Version with git](how-to/version-with-git.md).

## Render vs. Export Stems vs. Export Profile

All bounce audio from the **arrangement**, deterministically:

| Action | Produces |
|---|---|
| **Export Audio (WAV)** | One WAV of the whole mix |
| **Export Loop Region (WAV)** | Just the current loop selection |
| **Export Stems (WAV)** | One WAV per instrument track, into a folder |
| **Export Profile** | A *saved, named* target (`mix` / `range` / `track` / `stems`) you can re-run |

**Rule of thumb:** one-off bounce → Export Audio/Stems; a bounce you'll repeat
(same range every time, in CI) → define an
[export profile](../control-scripting/concepts/model.md#export-profile) once and
run it by name.

## Automation vs. Modulation vs. MIDI-learn

Three ways to move a parameter — they answer *what drives it*:

| Feature | Driven by | Use when… |
|---|---|---|
| **[Automation](../control-scripting/concepts/model.md#automation)** | Breakpoints you draw over the timeline | You want an exact, repeatable shape (a fade at bar 32) |
| **[Modulation](../control-scripting/concepts/model.md#modulation)** | An LFO (rate, depth, shape) | You want continuous movement without drawing (a wobble) |
| **[MIDI-learn](../control-scripting/concepts/model.md#controller-map)** | A live hardware knob / CC (or OSC) | You want to *perform* the change by hand |

**Rule of thumb:** drawn-and-fixed → Automation; hands-off-and-cyclic →
Modulation; play-it-live → MIDI-learn (the **Maps** window). Automation plays back
from the arrangement; the other two are live.

## Git undo vs. app Undo

| | **App Undo** (Ctrl+Z) | **Git** |
|---|---|---|
| Scope | Recent edits, this session | Durable history on disk |
| Survives a restart? | No | Yes |
| Can you name / compare / branch a state? | No | Yes — commits, tags, branches |

**Rule of thumb:** Undo to take back the last few moves; **commit** when you reach
a state worth returning to later. They're independent — Undo walks an in-memory
stack; git walks your saved history. → [Version with git](how-to/version-with-git.md),
[Keyboard shortcuts](reference/keyboard-shortcuts.md).
