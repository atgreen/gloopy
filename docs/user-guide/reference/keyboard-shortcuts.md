# Keyboard shortcuts

Every shortcut Gloopy responds to, grouped by where it applies. Modifier keys are
written for Windows/Linux; on macOS, **Ctrl** means **⌘** (Command). Press **?** at
any time for the same list as an in-app overlay.

## Global

These work anywhere in the main window.

| Key | Action |
|-----|--------|
| **Space** | Play / stop **in place** (keeps the playhead where it is) |
| **Tab** | Cycle the main view: **Arrange → Session → Mixer** |
| **Ctrl+S** | Save the project (prompts for a folder the first time) |
| **Ctrl+Shift+S** | Save As… (always prompts for a location) |
| **Ctrl+Z** | Undo |
| **Ctrl+Shift+Z** / **Ctrl+Y** | Redo |
| **Ctrl+.** | MIDI panic — all notes off (kills a stuck note) |
| **?** | Show the keyboard-shortcut overlay (press **?** again or **Esc** to close) |

!!! note "Space is play/pause, not play-from-start"
    Space toggles playback without moving the playhead, so you can audition,
    stop, and resume from the same spot. The transport's **Stop** button is what
    returns to the start.

## Transport & playhead

| Key | Action |
|-----|--------|
| **Home / End** | Move the playhead to the start / end of the project |
| **< / >** | Move the playhead back / forward one **bar** |
| **Shift+< / Shift+>** | Move the playhead back / forward one **beat** |

These apply whether or not playback is running.

## Recording

| Key | Action |
|-----|--------|
| **R** (or **F9**) | Start / stop recording |
| **Shift+R** | Arm / disarm the selected track |

## Loop & metronome

| Key | Action |
|-----|--------|
| **Ctrl+L** | Toggle the loop |
| **Ctrl+M** | Toggle the metronome |

## Clips (Arrangement)

Act on the selected clip.

| Key | Action |
|-----|--------|
| **Ctrl+C / Ctrl+X / Ctrl+V** | Copy / cut / paste the clip (paste lands at the playhead) |
| **Ctrl+D** | Duplicate the selected clip |
| **Ctrl+Shift+D** | Duplicate **linked** — the copy shares the original's pattern, so editing the notes of either updates both. "Make unique" (clip menu) detaches one again. |
| **Ctrl+E** | Split the selected clip at the playhead |
| **Delete / Backspace** | Delete the selected arrangement clip, or clear the selected Session slot |

## Timeline zoom & navigation

Active in the **Arrangement**; the same scroll / `W` / `F` / `E` vocabulary also works in
the **piano roll**. The arrangement has a pinned horizontal scrollbar; the piano roll has
both a horizontal (timeline) and a vertical (pitch) scrollbar.

| Key / gesture | Action |
|---------------|--------|
| **Ctrl+scroll** (or trackpad pinch) | Zoom the timeline horizontally, around the pointer |
| **Alt+scroll** | Zoom **track height** (in the piano roll: **pitch**) |
| **Ctrl+Alt+scroll** | Zoom the audio-clip **waveform amplitude** |
| **Shift+scroll** | Scroll the timeline horizontally |
| **Ctrl+drag** a box | Marquee — zoom to the box (horizontal + vertical) |
| **= / -** | Zoom in / out (horizontal) |
| **[ / ]** | Waveform amplitude out / in |
| **W** | Fit the song to the width |
| **H** | Fit all tracks to the height |
| **F** | Zoom to the selected clip (piano roll: frame the notes) |
| **E** | Zoom toggle — jump to the selection, press again to return |
| **1–5** | Recall a zoom preset (**Shift+1–5** stores the current view) |

Zoom and scroll position are saved with the project.

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
notes in the clip. (The zoom keys **W / F / E** above also apply here.)

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
