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
- **Track types** — *instrument* (synth, sampler, an **SFZ** instrument, or a
  hosted plugin), *audio* (import and play `.wav` clips with waveforms), and
  *MIDI out*.
- **Sound sources** — a one-shot **Sampler** (drag in a `.wav`), a polyphonic
  **Synth** (waveform + ADSR), and a vendored **sfizz** SFZ player. Ships with a
  procedurally-generated drum kit so it makes sound out of the box.
- **Plugin hosting** — load **VST3** and **LV2** instruments and effects
  (e.g. [Surge XT](https://surge-synthesizer.github.io/)); native plugin editors
  open when the plugin embeds an X11 UI, otherwise a generic parameter panel.
- **Piano-roll editing** — the same note model behind a 16-step **grid** and a
  **piano roll** (toggle STEPS / PIANO). The roll has vertical zoom + scroll,
  shift-drag **region selection** (move/transform groups), **note auditioning**
  (hear notes as you click/brush), **ghost notes** from other tracks, a
  **chord-stamp** tool, one-shot **strum** and **arpeggiate** actions, and
  **scale highlighting** + snap-to-scale drawing driven by the project scale.
- **Live arpeggiator** — a non-destructive per-track arp (rate, octaves, gate,
  mode up/down/up-down/random, **swing**, **hold**) that plays a track's chords as
  a pattern at playback without altering the clip.
- **Musical model** — a project **scale** (snap notes to key), a **tempo map**
  (tempo markers on the ruler), **automation** lanes and an **LFO/modulation**
  matrix on any parameter, and **MIDI-learn** to bind hardware CCs to any control.
- **Recording** — arm instrument/audio tracks, record MIDI and audio with a
  **punch** region and count-in; loop-recording stacks **takes** you can comp,
  promote, and clean up.
- **Mixer + effects** — tracks route to inserts → master (plus **aux buses/sends**
  and recallable **mixer scenes**), each with fader/pan/mute/solo/meters and an
  effect chain (**Gain, Filter, EQ, Delay, Reverb, Compressor, Bitcrusher,
  Waveshaper**, or any hosted plugin effect).
- **Live MIDI input** — play the selected instrument track from a MIDI keyboard;
  Gloopy opens available hardware inputs and exposes a virtual **"Gloopy MIDI In"**
  port that a controller or another app can connect to at any time.
- **Control API** — drive Gloopy from an external program (Common Lisp, Python,
  anything): **gRPC** for structural commands/queries and streamed feedback,
  **OSC** for low-latency live notes and knob turns. See
  [`docs/CONTROL-API.md`](docs/CONTROL-API.md).
- **Save / Load / Render** — projects persist either to a single `.gloopy` file
  (JUCE `ValueTree` → XML, with embedded sample and plugin-state data) or to a
  diff-friendly **composition-as-repo** directory (readable TOML + `.notes`/
  `.points` + WAV sidecars). `--render` bounces to a WAV offline, and named
  **export profiles** (mix / range / track / stems) drive batch renders.
- **Headless + scriptable** — every capability is reachable over the control API,
  plus offline CLI utilities (`scan`, `analyze`, `inspect`/`validate`/`pack`,
  `render` / `export-stems` — both with an optional `--range <startBeat> <endBeat>`) and
  loudness analysis (peak / true-peak / RMS / LUFS) for automation and CI.

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
./build/Gloopy_artefacts/Release/gloopy

# ...or open a project straight away:
./build/Gloopy_artefacts/Release/gloopy examples/demo-song.gloopy

# ...or bounce a project to a WAV without a GUI (whole song, or just a beat range):
./build/Gloopy_artefacts/Release/gloopy render examples/demo-song.gloopy out.wav
./build/Gloopy_artefacts/Release/gloopy render examples/demo-song.gloopy out.wav --range 0 16
# ...or split it into one WAV per instrument track:
./build/Gloopy_artefacts/Release/gloopy export-stems examples/demo-song.gloopy stems/
```

Complete demo songs live in [`examples/`](examples/): `demo-song.gloopy` (fully
portable) and `demo-song-surge.gloopy` (uses a baked Surge XT pad — install the
Surge XT VST3 to `~/.vst3` first).

JUCE is fetched automatically by CMake (`FetchContent`), pinned to `8.0.15`.
The gRPC C++ is generated from [`proto/gloopy.proto`](proto/gloopy.proto) at
build time using the system `protoc` / `grpc_cpp_plugin`.

### Embedded Surge XT synth (optional)

Gloopy can embed the **Surge XT** synth engine (its default instrument voice and
the browser's *Presets* tab). The engine source is the `third_party/surge` git
submodule. **Don't** `git clone --recursive` — that would pull Surge's own
`libs/JUCE` (~500 MB), which the synth core doesn't use. Instead, after cloning,
run the helper, which fetches only the libraries `surge-common` needs:

```sh
scripts/init-surge.sh        # inits third_party/surge + just the needed sub-libs
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # GLOOPY_WITH_SURGE is ON by default
```

The factory patches/wavetables are bundled in `third_party/surge-data/` (no extra
download). To build **without** Surge (a lean, pure-AGPL, C++17 binary), configure
with `-DGLOOPY_WITH_SURGE=OFF`. Surge XT is GPL-3.0 — see
[`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md).

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

...or drive it from Common Lisp via the `gloopy` system (`ocicl install ag-grpc`
first, then run `sbcl` from the repo root):

```lisp
(asdf:load-system :gloopy)
(in-package :gloopy)
(connect)                                     ; 127.0.0.1:50051
(let ((id (add-synth-track "Lead" :wave :saw)))
  (add-clip id :notes (list (note 60 0 1) (note 64 1 1) (note 67 2 1)))
  (play)
  (subscribe :seconds 3 :on-event #'print)    ; stream playhead + meters
  (stop)
  (render "/tmp/mix.wav"))                     ; offline bounce
```

...or from Python via the [`gloopy` client package](python/README.md)
(`pip install ./python`):

```python
from gloopy import Gloopy, note
with Gloopy() as g:
    lead = g.add_synth_track("Lead", wave="SAW")
    g.add_clip(lead, notes=[note(60, 0, 1), note(64, 1, 1), note(67, 2, 1)])
    g.play(); g.render("/tmp/mix.wav", tail_seconds=1.0)   # offline bounce
```

## Layout

See [`docs/PRD.md`](docs/PRD.md) for the product spec and architecture.

| Area | Files |
|------|-------|
| App shell / audio engine | `Source/Main.cpp`, `Source/MainComponent.*` |
| Model | `Track.h`, `Clip.h`, `Note.h`, `MixerTrack.h` |
| Generators | `Generator.h`, `Sampler.h`, `SfizzGenerator.h` (vendored sfizz), `SynthGenerator.h`, `SynthEngine.h`, `SynthVoice.h`, `DrumSynth.h`, `PluginInstrument.h` |
| Vendored | `third_party/sfizz/` (SFZ engine), `THIRD-PARTY-LICENSES.md` |
| Effects | `Effect.h`, `Effects.h`, `PluginEffect.h` |
| Plugin hosting | `PluginHost.h` |
| Transport | `Transport.h` |
| UI | `ArrangeView.*`, `PianoRoll.*`, `StepEditor.h`, `MixerView.*`, `GloopyLookAndFeel.*` |
| Control API | `OscControl.h`, `GrpcServer.*`, `proto/gloopy.proto` |
| Lisp clients | `gloopy.asd`, `examples/gloopy-grpc.lisp`, `gloopy-pb.lisp`, `gloopy-osc.lisp` |
| Python client | `python/gloopy/`, `python/example.py`, `python/README.md` |

## License

Gloopy is licensed under the GNU Affero General Public License, version 3
(AGPL-3.0-only) — see [`LICENSE`](LICENSE). This aligns with JUCE's GPL/AGPL
licensing terms.

When built with the embedded **Surge XT** synth (`-DGLOOPY_WITH_SURGE=ON`, the
default when the Surge source is present), the resulting binary is a combined
**GPL-3.0 / AGPL-3.0** work — Surge XT is GPL-3.0, which is compatible to combine
with the AGPL. Build with `-DGLOOPY_WITH_SURGE=OFF` for a lean binary with no
Surge code. See [`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md) for the full
component list and combined-work details.
