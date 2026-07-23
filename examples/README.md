# Example projects

## demo-song.gloopy

A complete ~44-bar demo track (124 BPM, A minor) that exercises the whole app:

- **8 tracks** — Kick, Snare, Hat, Clap (samplers), Bass, Pad, Lead (synths),
  and a Crash (audio track).
- **Song structure** — Intro → Verse → Chorus → Verse → Chorus → Outro, with
  tracks entering and dropping out per section.
- **Mixer effects** — reverb on the pad and crash, ping-pong-ish delay on the
  lead, a low-pass filter on the bass, a high-pass on the hats, and a master
  gain trim.

Open it via **File → Open**, or launch straight into it:

```sh
./build/Gloopy_artefacts/Release/Gloopy examples/demo-song.gloopy
```

### Sample credits
The drum/percussion one-shots are from the **Sonic Pi** sample set, which sources
them from **freesound.org** under **CC0 (public domain)**. They are embedded in
the project file, so it is fully self-contained.
