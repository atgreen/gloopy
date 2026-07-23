# Example projects

## demo-song.gloopy

A complete ~60-bar demo track (124 BPM, A minor) that exercises the whole app:

- **8 tracks** — Kick, Snare, Hat, Clap (samplers), Bass, Pad, Lead (synths),
  and an FX track (audio: crashes + synthesized risers).
- **Song structure** — Intro → Verse → Chorus → Verse → Chorus → **Breakdown**
  → **Final Chorus** → Outro, with drum fills and a filter-swept riser into
  every drop, octave-doubled leads in the choruses, and panned percussion.
- **Mixer effects** — reverb on the pad and crash, stereo ping-pong delay on
  the lead, a low-pass filter on the bass, a high-pass on the hats, and a
  master gain trim.

Open it via **File → Open**, or launch straight into it:

```sh
./build/Gloopy_artefacts/Release/Gloopy examples/demo-song.gloopy
```

## demo-song-surge.gloopy

The same arrangement, but the **Pad track is a hosted Surge XT (VST3)** instead
of the built-in synth — a warm, unison-detuned, low-passed pad patch baked
directly into the project's plugin state. Requires **Surge XT** installed (its
VST3 in `~/.vst3`); without it the Pad track simply loads silent.

### Sample credits
The drum/percussion one-shots are from the **Sonic Pi** sample set, which sources
them from **freesound.org** under **CC0 (public domain)**. They are embedded in
the project file, so it is fully self-contained.
