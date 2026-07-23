# Gloopy

A small MIDI + synth DAW built in C++ with [JUCE](https://juce.com/).

## What it does (MVP)

- **Built-in synthesizer** — 16-voice polyphony, selectable waveform
  (sine / saw / square / triangle) with an ADSR amplitude envelope.
- **Piano-roll sequencer** — draw, drag, resize and delete notes on a grid.
- **Transport** — play / pause / stop, adjustable tempo, and a looping timeline
  (16 beats by default).
- **On-screen keyboard** — play the synth live with the mouse or computer keys.

## Building

Requires a C++17 compiler, CMake ≥ 3.22, and (on Linux) the JUCE dev
dependencies: ALSA, FreeType and X11.

```sh
# Linux system deps (Fedora):
sudo dnf install -y alsa-lib-devel freetype-devel libX11-devel \
    libXrandr-devel libXinerama-devel libXcursor-devel libXext-devel

cmake -B build -G Ninja
cmake --build build
./build/Gloopy_artefacts/Gloopy      # (path may vary by generator/config)
```

JUCE itself is fetched automatically by CMake (`FetchContent`), pinned to
release `8.0.15`.

## Layout

| File | Role |
|------|------|
| `Source/Main.cpp`        | App entry point + main window |
| `Source/MainComponent.*` | Audio callback, transport bar, synth controls |
| `Source/PianoRoll.*`     | Editable piano-roll grid |
| `Source/Sequencer.h`     | Thread-safe note store → MIDI per block |
| `Source/SynthEngine.h`   | `juce::Synthesiser` wrapper |
| `Source/SynthVoice.h`    | Oscillator + ADSR voice |
| `Source/Transport.h`     | Tempo, playhead, loop, play state |
| `Source/Note.h`          | Note data model |
