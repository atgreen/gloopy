# Composition folder vs. `.gloopy` archive

Gloopy stores a project two ways — both hold the *same* thing, one is just zipped.

| | **Composition folder** | **`.gloopy` archive** |
|---|---|---|
| On disk | A directory of readable text files | A single zip file |
| Made by | **Save Project** (Ctrl+S) — the default | **Export Project (.gloopy)** |
| Opened by | **Open Composition Folder…** | **Open Archive…** (also `.zip`) |
| Git | **A git repository from the first save** | Not a repo (it's a share format) |
| Best for | Working, version control, readable diffs | Handing the whole project over as one file |

**Rule of thumb:** work in the **folder**; export a **`.gloopy`** to send. See
[Save vs. Export](../what-to-use-for-what.md#save-project-vs-export-project).

## What's inside the folder

The folder is designed to **diff and travel** — one small file per thing, so a
change touches only what it needs to (a no-op re-save writes nothing). You'll see:

| Path | Holds |
|------|-------|
| `gloopy.toml` | The manifest — title, tempo, time signature, the track list |
| `tracks/` | One `.toml` per track (its settings) |
| `clips/…/*.notes` | Each clip's notes, as readable text |
| `mixer/` | Inserts, effects, sends, groups |
| `automation/*.points` | Automation lanes |
| `tempo.toml`, `timesig.toml`, `scenes.toml`, `session.toml`, `locations.toml`, … | Tempo map, time-signature changes, scenes, the session grid, markers, and other settings |
| `assets/` | Binary sidecars — recorded/imported audio, plugin state |
| `.gitignore` | So git tracks the right files |

Because it's plain text, a git **diff** shows real changes — "filter cutoff
0.3 → 0.5", not an opaque blob. That's the whole point of the format.

## Both round-trip losslessly

Opening either form gives you an identical project; the loader auto-detects a
folder vs. an archive. `.gloopy` is simply a zip of the folder — you can unzip one
by hand and it's a normal composition folder inside.

## See also

- [Two forms on disk](../../control-scripting/concepts/model.md#on-disk-two-forms) —
  the model-level definition.
- [Version your project with git](../how-to/version-with-git.md) — what the folder
  format unlocks.
