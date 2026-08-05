# Link clips to edit them together

**Task:** a part repeats through the song — a drum groove, a bassline, a chord stab —
and you want to tweak it in one place and have every repeat follow. Gloopy's **linked
clips** share one pattern: edit the notes of any instance and they all update.

## Make a linked copy

1. Select a clip in the arrangement.
2. Press **Ctrl+Shift+D** (macOS **⌘⇧D**), or right-click the clip and choose
   **Duplicate linked**.

The copy is butted up after the original and marked with a small **chain badge**. A plain
**Ctrl+D** duplicate is *independent* as before — only the linked variant shares a pattern.

## Edit once, update everywhere

Editing the **notes** of any linked clip propagates to the whole group:

- draw or move notes in the **piano roll** or **step grid**, or
- run a note transform (quantize, transpose, humanize, strum, …).

Hover any clip in a group and the **related clips highlight** so you can see what moves
together.

## What stays per-instance

Only the pattern (the notes and the loop window) is shared. Each clip keeps its own
**position, length, loop on/off, transpose, velocity, and colour** — so the same riff can
sit at bar 8 and bar 24, one of them a fifth up, both fixing the same wrong note.

## Detach a clip

- **Make unique** (in the clip's right-click menu) forks a clip into its own private
  pattern, leaving the rest of the group linked.
- Ops that reshape a single clip — **split, crop, time-stretch, consolidate** — detach it
  automatically, since they diverge its content.

Links are saved with the project, so they survive a reload.

## See also

- [Clip](../../control-scripting/concepts/model.md#clip) — the underlying model.
- [Keyboard shortcuts](../reference/keyboard-shortcuts.md#clips-arrangement).
