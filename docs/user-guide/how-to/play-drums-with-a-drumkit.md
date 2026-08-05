# Play real drums with a DrumKit

**Task:** get real, sampled drums on a single track — kick, snare, hat and the rest —
instead of one synth track per drum. A **DrumKit** is one instrument holding many
sampled **pads**, each bound to a MIDI note, so a whole groove lives in one clip.

## Add a drum-kit track

1. Click **+ Track** and choose **Drum Kit**.
2. Pick a bundled kit — **GMRockKit** (a sampled acoustic kit) or **TR-808**
   (electronic) — or choose **Load Hydrogen kit…** to import your own.

The track appears with one pad per drum voice. Each pad plays its sample at its natural
pitch when its MIDI note fires, so a kick, snare and hat coexist in one clip.

## Import a Hydrogen kit

**Load Hydrogen kit…** accepts any of:

- a `.h2drumkit` archive,
- a `drumkit.xml`, or
- a kit folder.

Gloopy reads each instrument's loudest velocity layer and lays the voices out as pads.

## Program the groove

A DrumKit is edited like any MIDI clip, and the kit **shapes the editor**:

- In the **step grid** (the editor's **STEPS** tab), each pad is its own **lane** — click
  cells to place hits, watch the playhead sweep the columns.
- In the **piano roll**, each pad sits on its own note row.

Both are views over the same notes, so they always agree.

## Self-contained projects

Saving a project stores the kit inside the composition (`type = "drumkit"`, one
`[[generator.pad]]` per voice referencing its sample under `assets/`), so it stays
human-readable and portable. The bundled sample demos (demo-song, demo-synthwave, …)
ship their drums this way — one DrumKit track each.

## See also

- [The clip editor](../reference/editor.md#step-grid) — the step grid, lane per pad.
- [Composition formats](../reference/composition-formats.md) — how a project is stored on
  disk.
