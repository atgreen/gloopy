# Gloopy

A linear-arranger DAW. Lay instrument, audio, and MIDI **tracks** down a
timeline; each track owns its **clips**. Edit notes in a step grid or piano roll,
drive instruments with the built-in
synth/sampler or hosted **VST3 / LV2 plugins**, shape the sound through a
**mixer** with insert effects — and drive the whole thing remotely over a
**control API** (OSC + gRPC).

This manual has **two front doors**. Pick the one that matches what you're doing.

<div class="grid cards" markdown>

-   :material-music: **User guide**

    ---

    For musicians and producers working inside the app: recording, arranging,
    editing, mixing, and bouncing to WAV.

    [:octicons-arrow-right-24: Start here](user-guide/index.md)

-   :material-console: **Control & scripting**

    ---

    For integrators, script authors, and hardware vendors driving Gloopy over
    gRPC or OSC, or building on the Python and Common Lisp clients.

    [:octicons-arrow-right-24: Start here](control-scripting/index.md)

</div>

## New here?

- Musician → [Record your first track](user-guide/tutorials/first-track.md).
- Developer → [Drive Gloopy from Python](control-scripting/tutorials/python-quickstart.md),
  then read [The Gloopy model](control-scripting/concepts/model.md).

Whichever door you came through, the vocabulary — *track*, *clip*, *scene*,
*transport*, *mixer track* — is defined once, in
[**The Gloopy model**](control-scripting/concepts/model.md). Every other page
links there rather than re-explaining it.
