# Keyboard shortcuts

Every shortcut Gloopy responds to, grouped by where it applies. Modifier keys are
written for Windows/Linux; on macOS, **Ctrl** means **⌘** (Command).

## Global

These work anywhere in the main window.

| Key | Action |
|-----|--------|
| **Space** | Play / stop **in place** (keeps the playhead where it is) |
| **Tab** | Cycle the main view: **Arrange → Session → Mixer** |
| **Ctrl+S** | Save the project (prompts for a folder the first time) |
| **Ctrl+Z** | Undo |
| **Ctrl+Shift+Z** / **Ctrl+Y** | Redo |
| **Ctrl+.** | MIDI panic — all notes off (kills a stuck note) |

!!! note "Space is play/pause, not play-from-start"
    Space toggles playback without moving the playhead, so you can audition,
    stop, and resume from the same spot. The transport's **Stop** button is what
    returns to the start.

## Mixer and Session views (grouping)

Available while the **Mixer** or **Session** view is focused, acting on the
current strip / track-column selection:

| Key | Action |
|-----|--------|
| **Ctrl+G** | Group the selected strips / track columns into a submix |
| **Ctrl+Shift+G** | Ungroup the selected group |
| **Ctrl+Shift+F** | Fold / unfold all groups |

See [Mixer track (bus, send)](../../control-scripting/concepts/model.md#mixer-track-bus-send)
for what a group is.

## Piano roll (the editor)

Active when the **PIANO** editor has focus and a clip is loaded. Each transform
acts on the **selected notes** if you've made a selection, otherwise on **all**
notes in the clip.

| Key | Action |
|-----|--------|
| **↑ / ↓** | Transpose by a semitone (**Shift** = a full octave) |
| **Q** | Quantize note starts to 1/16 (**Shift+Q** = 1/8) |
| **H** | Humanize — nudge timing and velocity slightly |
| **S** | Strum — spread a chord's notes in time (**Shift+S** reverses the direction) |
| **L** | Legato — stretch each note to the next one's start (**Shift+L** = halfway) |
| **Delete / Backspace** | Delete the selected notes |

These are the same operations offered by the editor's **SCALE / KNIFE / ARP /
LEGATO / STRUM** toolbar tabs — the keys are the quick path once a clip is open.

## Dialogs

In the commit and other git dialogs (and the built-in prompts):

| Key | Action |
|-----|--------|
| **Esc** | Cancel / close the dialog |
| **Ctrl+Enter** | Confirm (Commit, Save, Create…) |

## See also

- [Version your project with git](../how-to/version-with-git.md) — the File-menu
  git surface these dialog keys drive.
- [Control & scripting](../../control-scripting/index.md) — everything a shortcut
  does is also reachable over the API, for scripts and control surfaces.
