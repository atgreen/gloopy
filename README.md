# Gloopy

A linear-arranger DAW built in C++ with [JUCE](https://juce.com/).

Lay instrument, audio, and MIDI **tracks** down a timeline; each track owns its
**clips**. Edit a clip's notes in a **step grid** or a **piano roll**, drive
instruments with the built-in synth/sampler or hosted **VST3 / LV2 plugins**,
shape the sound through a **mixer** with insert effects, and drive the whole
thing remotely over a **control API** (OSC + gRPC). Save the project to disk or
bounce it to a WAV.

## Features

- **Linear arranger** — tracks arranged down a timeline; every clip owns its own
  content (MIDI notes or audio), positioned and looped independently.
- **Track types** — *instrument* (synth, sampler, or a hosted plugin), *audio*
  (import and play `.wav` clips with waveforms), and *MIDI out*.
- **Sound sources** — a one-shot **Sampler** (drag in a `.wav`) and a polyphonic
  **Synth** (waveform + ADSR). Ships with a procedurally-generated drum kit so
  it makes sound out of the box.
- **Plugin hosting** — load **VST3** and **LV2** instruments and effects
  (e.g. [Surge XT](https://surge-synthesizer.github.io/)); native plugin editors
  open when the plugin embeds an X11 UI, otherwise a generic parameter panel.
- **Clip editor** — the same note model behind a 16-step **grid** and a
  **piano roll** (toggle STEPS / PIANO), with a black/white key gutter.
- **Mixer + effects** — tracks route to inserts → master, each with
  fader/pan/mute/solo/meters and an effect chain (**Gain, Filter, Delay,
  Reverb**, or any hosted plugin effect).
- **Control API** — drive Gloopy from an external program (Common Lisp, Python,
  anything): **gRPC** for structural commands/queries and streamed feedback,
  **OSC** for low-latency live notes and knob turns. See
  [`docs/CONTROL-API.md`](docs/CONTROL-API.md).
- **Save / Load / Render** — projects persist to a `.gloopy` file (JUCE
  `ValueTree` → XML) with embedded sample and plugin-state data; `--render`
  bounces a project to a WAV offline.

## Building

Requires a C++17 compiler and CMake ≥ 3.22. The build also links gRPC and
Protocol Buffers (for the control API) and, on Linux, the JUCE dev
dependencies: ALSA, FreeType and X11.

```sh
# Linux system deps (Fedora):
sudo dnf install -y alsa-lib-devel freetype-devel libX11-devel \
    libXrandr-devel libXinerama-devel libXcursor-devel libXext-devel \
    grpc-devel protobuf-devel protobuf-compiler grpc-plugins

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Gloopy_artefacts/Release/Gloopy

# ...or open a project straight away:
./build/Gloopy_artefacts/Release/Gloopy examples/demo-song.gloopy

# ...or bounce a project to a WAV without a GUI:
./build/Gloopy_artefacts/Release/Gloopy --render examples/demo-song.gloopy out.wav
```

Complete demo songs live in [`examples/`](examples/): `demo-song.gloopy` (fully
portable) and `demo-song-surge.gloopy` (uses a baked Surge XT pad — install the
Surge XT VST3 to `~/.vst3` first).

JUCE is fetched automatically by CMake (`FetchContent`), pinned to `8.0.15`.
The gRPC C++ is generated from [`proto/gloopy.proto`](proto/gloopy.proto) at
build time using the system `protoc` / `grpc_cpp_plugin`.

## Quick start

1. Press **Play** — the loaded demo arrangement plays down the timeline.
2. Click a clip to load it into the editor; toggle **STEPS** / **PIANO** to edit
   its notes as a step grid or in the piano roll.
3. Add tracks with **+ Synth**, **+ Sample**, **+ Audio**, or **+ Plugin**.
4. Open the **Mixer** to add insert effects and balance levels.
5. **File → Save As** to store the project; **File → Open** to reload it.

## Control API

Gloopy listens on two ports at startup: **OSC** on UDP `9000` and **gRPC** on
`127.0.0.1:50051`. External programs can create tracks, hand over MIDI
sequences, add/tweak effects, drive instruments live, and subscribe to the
playhead and meters. Two worked Common Lisp clients live in `examples/`: the structural gRPC surface
(the [`gloopy`](gloopy.asd) ASDF system — `ocicl install ag-grpc` then
`(asdf:load-system :gloopy)`, built on [ag-grpc](https://github.com/atgreen/ag-grpc))
and [`gloopy-osc.lisp`](examples/gloopy-osc.lisp) (the live OSC lane). The full
design and the gRPC/OSC split are documented in
[`docs/CONTROL-API.md`](docs/CONTROL-API.md).

```sh
# poke it with grpcurl (structural commands & queries):
grpcurl -plaintext -proto proto/gloopy.proto -import-path proto \
    127.0.0.1:50051 gloopy.v1.Gloopy/GetState
```

## Layout

See [`docs/PRD.md`](docs/PRD.md) for the product spec and architecture.

| Area | Files |
|------|-------|
| App shell / audio engine | `Source/Main.cpp`, `Source/MainComponent.*` |
| Model | `Track.h`, `Clip.h`, `Note.h`, `MixerTrack.h` |
| Generators | `Generator.h`, `Sampler.h`, `SynthGenerator.h`, `SynthEngine.h`, `SynthVoice.h`, `DrumSynth.h`, `PluginInstrument.h` |
| Effects | `Effect.h`, `Effects.h`, `PluginEffect.h` |
| Plugin hosting | `PluginHost.h` |
| Transport | `Transport.h` |
| UI | `ArrangeView.*`, `PianoRoll.*`, `StepEditor.h`, `MixerView.*`, `GloopyLookAndFeel.*` |
| Control API | `OscControl.h`, `GrpcServer.*`, `proto/gloopy.proto` |
