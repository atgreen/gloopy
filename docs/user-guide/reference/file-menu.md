# File menu reference

Everything under the toolbar **File** button, by group.

## New and open

| Item | What it does |
|------|--------------|
| **New Project (empty)** | Start a blank project |
| **New from Template ▸** | Start from a built-in or saved template |
| **Open Composition Folder…** | Open a project **folder** (the working format) |
| **Open Archive…** | Open a single-file `.gloopy` / `.zip` project |
| **Import MIDI File…** | Load a `.mid` — a synth track + clip per MIDI track |

## Save

| Item | What it does |
|------|--------------|
| **Save Project** (Ctrl+S) | Save the [composition folder](composition-formats.md); prompts for a location the first time |
| **Save Project As…** | Save a copy into a new folder |
| **Save as Template…** | Save the current project as a reusable template |

Every saved folder is a git repository — see
[Version your project with git](../how-to/version-with-git.md).

## Export

| Item | What it does |
|------|--------------|
| **Export Project (.gloopy)…** | The whole project as a single-file **zip** archive (to share) |
| **Export MIDI File…** | The project as a Type-1 `.mid` (loops tiled out) |
| **Export Audio (WAV)…** | Offline bounce of the whole mix |
| **Export Loop Region (WAV)…** | Bounce just the loop selection (enabled when Loop is on) |
| **Export Stems (WAV)…** | One WAV per instrument track, into a folder |

Recipe: [Export the mix, a range, a track, or stems](../how-to/export-audio.md).
(A single **track** is exported from its [header menu](track-header.md), not here.)

## Tuning and notes

| Item | What it does |
|------|--------------|
| **Load Tuning (.scl)…** | Import a Scala microtuning file |
| **Reset Tuning (Equal)** | Return to 12-tone equal temperament |
| **Project Notes…** | Free-text notes saved with the project |

## Source control (git)

A full git surface — **Source Control**, **Commit…**, **History…**,
**Branches…**, **Tags…**, **Open at version…**, **Changes (Diff)…**, **Discard /
Stash / Reset…**, **Remotes / Push / Pull…**, **Resolve conflicts…**, and **Git
Settings…**. All documented in
[Version your project with git](../how-to/version-with-git.md).

## Devices

| Item | What it does |
|------|--------------|
| **MIDI Inputs ▸** | Read-only: the input sources Gloopy hears and which track they play |
| **Rescan Plugins** | Re-scan installed VST3/LV2 plugins |
| **Start Lisp REPL (SWANK)…** | Launch a warm Lisp kernel with a SWANK server and show its port, so you can attach SLIME/Sly and develop [script-clip](../how-to/generate-notes-from-code.md) generators interactively |

## About

| Item | What it does |
|------|--------------|
| **About Gloopy…** | Version, license (AGPL-3.0), bundled components, and the source-code offer |
