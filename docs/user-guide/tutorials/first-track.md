# Your first track

**You will:** open Gloopy, play a demo song, add your own instrument track, draw
a short looping melody, and hear it play alongside the demo. About 10 minutes.
No MIDI keyboard or extra setup needed.

By the end you'll know your way around the four things every Gloopy session is
made of: the **transport**, **tracks**, **clips**, and the **editor**.

!!! info "Before you start"
    You need Gloopy installed and the bundled `examples/` folder alongside it.
    Everything here works with a mouse — we'll cover recording from a keyboard in
    a later guide.

## 1. Open a song and press Play

Launch Gloopy on the portable demo:

```sh
gloopy examples/demo-song
```

A row of **tracks** runs down the left; each track's **clips** sit along the
timeline to the right. Press the green **Play** button (or watch the playhead
sweep across as the song plays). Press **Stop** to return to the start.

That green triangle and the tempo readout next to it are the **transport** — the
one clock the whole song follows.

## 2. Look inside a clip

Click any clip in the timeline. It loads into the **editor** at the bottom of the
window. Toggle between **STEPS** and **PIANO**:

- **STEPS** — a 16-step grid, quick for drums and simple patterns.
- **PIANO** — a piano roll, better for melodies; scroll and zoom vertically.

Both edit the *same notes* — a clip owns its content, and these are just two ways
to see it. Don't worry about changing the demo; we'll add our own track next.

## 3. Add an instrument track

In the toolbar, click **+ Synth**. A new instrument track appears at the bottom
of the track list, ready to play notes.

!!! tip
    **+ Synth**, **+ Sample**, **+ Audio**, and **+ Plugin** all add tracks —
    they differ only in what makes the sound. They're all the same kind of track
    underneath (see [The Gloopy model](../../control-scripting/concepts/model.md#track)).

## 4. Draw a short loop

1. On your new track, make a clip on the timeline near the start of the song.
2. Click it to open it in the editor, and make sure **STEPS** is selected.
3. Click a few steps to place notes — try four evenly spaced notes for a simple
   pulse. Each note you place, you'll hear (**note auditioning**).

Your clip is short, but a clip **loops** by default: placed across a longer span
it just repeats. So a one-bar idea fills the whole section without extra work.

## 5. Play it together

Press **Play**. Your loop plays in time with the demo — one shared transport
keeps everything locked. Too loud or too quiet? Use the track's **volume**
control in its header, or open the **Mixer** (toolbar) to balance levels and,
later, add effects.

## 6. Save your work

**File → Save Project** (or Ctrl+S) — your project is saved as a **composition
folder**, a directory of readable text files that works well with version control
(it's a git repository from the first save). This is the working format.

To share the project as one file, use **File → Export Project (.gloopy)** — that
same folder zipped into a single portable archive. Reopen either with **File →
Open** (a folder) or **File → Open Archive** (a `.gloopy`/`.zip`).

## What you learned

You met the four core nouns — **transport**, **track**, **clip**, **note** — and
saw that clips loop and share one clock. Each of those is defined precisely, once,
in [**The Gloopy model**](../../control-scripting/concepts/model.md).

**Next:**

- Arrange, edit, and mix in depth → [User guide](../index.md).
- Automate Gloopy from code → [Drive Gloopy from Python](../../control-scripting/tutorials/python-quickstart.md).
