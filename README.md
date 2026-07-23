# Gloopy

An FL Studio–inspired, pattern-based DAW built in C++ with [JUCE](https://juce.com/).

Build short **patterns** on a **step sequencer**, layer melodies in a **piano
roll**, arrange the patterns into a full song on the **playlist**, shape the
sound through a **mixer** with insert effects, and save the whole project to
disk.

## Features

- **Channel Rack + Step Sequencer** — stack channels (sample players or the
  built-in synth), program 16-step patterns, per-channel mute/volume.
- **Sound sources** — a one-shot **Sampler** (drag in a `.wav`) and a 16-voice
  **Synth** (waveform + ADSR). Ships with a procedurally-generated drum kit
  (kick/snare/hat/clap) so it makes sound out of the box.
- **Patterns + Piano Roll** — multiple patterns; the step grid and a per-channel
  piano roll edit one unified note model.
- **Playlist + Song mode** — paint pattern clips onto a multi-track timeline
  (clips repeat their pattern to fill); toggle Pattern/Song playback.
- **Mixer + effects** — channels route to inserts → master, each with
  fader/pan/mute/solo/meters and an effect chain (**Gain, Filter, Delay,
  Reverb**).
- **Save / Load** — projects persist to a `.gloopy` file (JUCE `ValueTree` →
  XML), including embedded sample data. New / Open / Save / Save As.

## Building

Requires a C++17 compiler, CMake ≥ 3.22, and (on Linux) the JUCE dev
dependencies: ALSA, FreeType and X11.

```sh
# Linux system deps (Fedora):
sudo dnf install -y alsa-lib-devel freetype-devel libX11-devel \
    libXrandr-devel libXinerama-devel libXcursor-devel libXext-devel

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Gloopy_artefacts/Release/Gloopy
```

JUCE is fetched automatically by CMake (`FetchContent`), pinned to `8.0.15`.

## Quick start

1. Press **Play** — a seeded demo groove loops (Pattern mode).
2. Click steps in the **Channel Rack** to change the beat; click a channel's
   **PR** button to draw notes in the **Piano Roll**.
3. Make variations with **+ Pat**, then **paint clips** in the **Playlist**.
4. Toggle **Song** and press Play to hear the whole arrangement.
5. Open the **Mixer** to add effects and balance levels.
6. **File → Save As** to store the project; **File → Open** to reload it.

## Layout

See [`docs/PRD.md`](docs/PRD.md) for the full product spec and architecture.

| Area | Files |
|------|-------|
| App shell / audio engine | `Source/Main.cpp`, `Source/MainComponent.*` |
| Model | `Note.h`, `Channel.h`, `Pattern.h`, `PlaylistClip.h`, `MixerTrack.h` |
| Generators | `Generator.h`, `Sampler.h`, `SynthGenerator.h`, `SynthEngine.h`, `SynthVoice.h`, `DrumSynth.h` |
| Effects | `Effect.h`, `Effects.h` |
| Transport / scheduling | `Transport.h`, `NoteScheduler.h` |
| UI | `ChannelRackView.*`, `PianoRoll.*`, `PlaylistView.*`, `MixerView.*` |
