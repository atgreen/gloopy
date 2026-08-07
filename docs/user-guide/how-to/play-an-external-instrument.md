# Play an external instrument

**Task:** use a standalone synth application — [ZynAddSubFX](https://zynaddsubfx.sourceforge.io/),
Yoshimi, and friends — as an instrument track, with its own real window, played
from Gloopy and mixed back into your song. Assumes you've done
[Your first track](../tutorials/first-track.md). *(Linux.)*

Some synths are richest as their own apps, with an interface Gloopy can't
reproduce. Instead of hosting them, Gloopy launches one as a separate program and
wires it in as a track: you play it from Gloopy, tweak its sound in *its* window,
and its audio flows into your mix through the track's inserts and fader.

## Add one

1. Click **+ Track** and open the **External Instrument** submenu.
2. Pick a synth from the list — Gloopy shows the standalone synths it found
   installed — or choose **Custom command…** to launch anything else by name.
3. The synth opens in **its own window**, and a new track appears for it. Its
   window may take a moment to come up; Gloopy brings it to the front for you.

## Play it and hear it in the mix

1. **Select the track** (click its header) and play — from a
   [MIDI keyboard](../how-to/record-midi-and-audio.md#record-midi) or from clips
   you add to the track, exactly like any instrument. The notes go to the synth.
2. Its sound comes **back into the track**, so the track fader, pan, mute, and any
   **[insert effects](effects-and-mixing.md)** apply to it, and it's part of the
   render.
3. **Design the sound in the synth's own window** — patches, filters, effects. That
   window is the synth's; Gloopy just plays it and captures the result.

## Notes

- **Finding synths.** Gloopy lists the standalone synths it can find on your
  system; if yours isn't there, use **Custom command…** and type how you'd launch
  it from a terminal.
- **Its window is separate.** Closing the synth's window (or removing the track)
  stops the instrument. Keep it open while you work with the track.
- **It's saved with the project.** The track remembers which app it is, so opening
  the project launches it again.

You can also add and play an external instrument [from a script](../../control-scripting/index.md)
(`add_external_instrument` in the control API).
