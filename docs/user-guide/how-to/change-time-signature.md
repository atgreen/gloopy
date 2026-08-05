# Change time signature mid-song

**Task:** a piece isn't locked to one meter — a 4/4 verse drops into a 6/8 bridge, or a
5/4 intro resolves to 4/4. Gloopy lets you place **time-signature changes** at any bar,
and the bar grid, numbering, metronome and readout all follow.

## Set the initial signature

Right-click the **arrangement ruler** and choose **Initial time signature…** to set the
bar-1 meter for the whole piece (or use the signature box in the toolbar).

## Add a change at a bar

1. Right-click the **ruler** at the bar where the meter should change.
2. Choose **Add time-signature change here…** and enter the new numerator / denominator
   (e.g. `6/8`).

From that bar onward:

- the **bar grid** re-spaces to the new meter and the bars **renumber** continuously
  across the change,
- the **metronome** accents the new downbeats, and
- the **`bar . beat . tick`** position readout counts in the new meter.

To remove one, right-click on the change and choose **Remove time-signature change**.

## Nothing moves but the barlines

Clips and notes are positioned in **beats**, not bars, so inserting or removing a
time-signature change **never shifts your material** — it only changes the barlines drawn
over it, where the metronome accents fall, and how positions are counted. Changes are
stored in the project (`timesig.toml`) and are **undoable**.

!!! note "3/4 is not 6/8"
    Time signatures are kept as written — `3/4` stays distinct from `6/8` even though
    they span the same number of beats — so the grid and accents match how you notated it.

## See also

- [The musical model](../explanation/the-musical-model.md) — why positions are in beats.
- [Keyboard shortcuts](../reference/keyboard-shortcuts.md#transport-playhead).
