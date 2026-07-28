# Record MIDI and audio

**Task:** capture a live performance — MIDI from a keyboard, or audio from an
input — onto a track. Assumes you've done [Your first track](../tutorials/first-track.md).

## Record MIDI

1. **Select an instrument track** (click its header). MIDI records into the
   selected instrument track; if none is selected, it goes to the first one.
2. Play some notes to check you're being heard — the track's **live-MIDI LED**
   pulses green when notes arrive.
3. Press the toolbar **Record** button (or set a **Loop** region first to
   loop-record — see below). Recording starts.
4. Play. Press **Stop** (or **Record** again) to finish — your performance becomes
   a clip on that track.

!!! tip "No keyboard? Draw instead"
    You don't need to record to make MIDI — click notes into the **STEPS** grid or
    **PIANO** roll in the editor. Recording is for playing a part in live.

## Record audio

Audio tracks capture from your audio input.

1. Add an **audio** source (or an empty audio track), then **arm** it: click the
   red **●** button in the track header. Arm every audio track you want to
   capture — Record captures all armed audio tracks at once.
2. Press the toolbar **Record**. Gloopy opens a 24-bit take file per armed track.
3. Press **Stop** to finish; the recording lands as an audio clip on the track.

## Punch in and out

To re-record just a section, set a **punch region**: **Alt-drag** across the
arrangement ruler to mark the in/out range (it shows as a red brace). Recording
only writes inside that range. **Alt-double-click** the ruler clears it.

## Loop-record and stack takes

Turn on **Loop** (toolbar) with a region set, then Record. Each time the loop
wraps, Gloopy keeps the previous pass as a muted **take** and starts a fresh one —
so you can play a phrase several times and choose the best after. Comping those
takes is its own recipe → [Comp, promote, and clean up takes](comp-takes.md).

!!! note "Count-in, monitoring, and record format"
    A metronome **count-in**, input **monitoring**, and the record **format**
    (WAV/FLAC) are set through the [control API](../../control-scripting/index.md)
    rather than a desktop control — arm and monitoring options live on the
    recording RPCs. The **Metro** toolbar button toggles the click itself.

## See also

- [Comp, promote, and clean up takes](comp-takes.md) — choosing the best of
  several loop-recorded passes.
- [Keyboard shortcuts](../reference/keyboard-shortcuts.md) — **Space** to
  start/stop playback while you work.
