# AGENTS.md — working notes for AI agents

Gloopy is a linear-arranger DAW in C++17 / JUCE 8 (see `README.md`, `docs/PRD.md`).
This file captures the non-obvious things that are easy to get wrong.

## Build & run

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # JUCE is fetched via FetchContent
cmake --build build --target Gloopy                  # incremental; LTO is off for speed
./build/Gloopy_artefacts/Release/Gloopy [project.gloopy]
./build/Gloopy_artefacts/Release/Gloopy --render in.gloopy out.wav   # headless bounce
```
Needs the gRPC/protobuf and JUCE dev deps in `README.md`. The C++ is generated from
`proto/gloopy.proto` at build time. This is a GUI app but runs on the current X display.
JUCE is **pinned at `8.0.15`** in `CMakeLists.txt` (FetchContent). The embedded **surge**
vendors its *own* JUCE under `third_party/surge/libs/JUCE`, separate from Gloopy's — a
JUCE bump moves only the FetchContent pin, not surge's copy.

## Composition (directory) format — Phase 1

Besides the single-file `.gloopy` XML (still the default), Gloopy can serialise a
project as a **composition directory** for diff-friendly, "song as repo" workflows
(design: `gloop-compositions.md`). `Source/Composition.cpp` maps the same ValueTree
that `toValueTree`/`loadFromTree` use to a tree of TOML manifests + line-oriented
`.notes`/`.points` files + binary WAV/plugin sidecars:

```
gloopy.toml  tracks/<slug>.toml  clips/<track>/<clip>.notes
mixer/{inserts,effects}.toml  automation/{lanes.toml,<slug>.points}
assets/samples/*.wav  plugins/state/*  .gitignore
```

`SaveComposition`/`LoadComposition` gRPC RPCs drive it, and it's first-class in the
File menu (open a `.gloopy` / composition folder / `.zip`; save-as either format;
`LoadProject` auto-detects a directory / `gloopy.toml` / `.zip`). Saves are
**content-addressed** (dirty-file tracking): a no-op re-save writes nothing, one
fader rewrites one file, removed objects prune their files, and `dir → runtime →
dir` is byte-stable — so don't expect a `deleteRecursively`+rewrite. TOML is a
small hand-rolled subset (`Source/Toml.h`, unit-tested). Round-trip is verified in
`tests/smoke.sh` (round-trip render, no-op-write, zip load) and matches within
VPO's per-note humanisation noise.

## Audio recording (Phase 1)

`Source/Recording.cpp` captures live input to a WAV take and creates a *referencing*
audio clip (design: `recording.md`). The device opens inputs (`setAudioChannels(2,2)`);
the audio thread copies input into a JUCE `ThreadedWriter` (FIFO + background thread)
— never allocate/open/close/block on the audio thread. Takes go to a composition's
`assets/recordings/` + `recordings/takes.toml`; the clip references them by path
(`Clip.audioFile`/`takeId`), serialised as a reference (not embedded) in both
`.gloopy` and the composition format — so `assets/recordings/*.wav` is kept, not
pruned, on save. API: `ListAudioInputs`, `ArmTrack`; the existing
`StartRecording`/`StopRecording` also drive armed audio tracks. To self-test without
a mic (e.g. headless/CI), launch with `GLOOPY_REC_TEST_TONE_HZ=440` — it injects a
tone in place of the input, so a real record→take→clip→playback cycle is verifiable.

## Tests

```sh
cmake -B build -G Ninja -DGLOOPY_TESTS=ON        # opt in; off by default
cmake --build build --target GloopyTests
ctest --test-dir build --output-on-failure       # NoteScheduler/swing + ValueTree round-trip
./tests/smoke.sh                                 # boots the app, drives gRPC, renders, checks non-silent
```
CI (`.github/workflows/ci.yml`) runs all of the above on push/PR under `xvfb`.

## Driving it headless

Gloopy opens two control ports at startup: **OSC** UDP `9000` (live notes/knobs) and
**gRPC** `127.0.0.1:50051` (structural/queries). Full surface: `proto/gloopy.proto`,
design in `docs/CONTROL-API.md`. Everything below is scriptable via `grpcurl`:

```sh
G="grpcurl -plaintext -proto proto/gloopy.proto -import-path proto"
$G -d '{}' 127.0.0.1:50051 gloopy.v1.Gloopy/GetState
$G -d '{"bpm":100}' 127.0.0.1:50051 gloopy.v1.Gloopy/SetTempo
```
Or from Common Lisp: `(asdf:load-system :gloopy)` (the `gloopy` system, needs
`ocicl install ag-grpc`). To render a project without a GUI, use `RenderToFile`
(fast, offline) or the `--render` CLI.

## GUI verification (screenshots, under Xvfb)

Every UI slice is screenshot-validated. The live desktop here is **Wayland**, so an X11
grab of it is black — capture on a headless **Xvfb** display instead:

```sh
rm -f /tmp/.X99-lock                          # a stale lock silently blocks the launch
Xvfb :99 -screen 0 1600x1000x24 & export DISPLAY=:99
./build/Gloopy_artefacts/Release/Gloopy &     # then drive it over gRPC
xdotool ...                                   # select a clip / click a tab
ffmpeg -f x11grab -video_size 1600x1000 -i :99 -frames:v 1 shot.png
```
- **`pkill -x Gloopy`, never `pkill -f gloopy`** — the scratchpad path contains "gloopy",
  so a `-f` pattern also matches (and kills) your own harness/shell.
- Menus (esp. mixer routing) are **coordinate-flaky** under xdotool — for audio-logic
  checks prefer a debug log or a `RenderToFile` assertion over reading a meter screenshot.

## Landmines (verified the hard way)

- **proto3 omits zero.** JSON from grpcurl drops `int32`/`bool` fields equal to 0/false.
  Track id 0 (the first/Kick track) has **no `id` key** — always `t.get('id', 0)`.
- **One instance at a time.** A second Gloopy fails to bind 50051 and your grpcurl
  silently hits the *old* instance. Kill with `pkill -x Gloopy` (NOT `pkill -f Gloopy`
  — the `-f` also matches your own shell). Check with `pgrep -x Gloopy`.
- **`NewProject` resets to the default 5-track kit** (Kick/Snare/Hat/Clap/Bass, the
  only source of the procedural drum samplers). To reuse the kick, keep that track and
  clear its seeded clip; don't remove-all if you need drums.
- **Level staging is manual** — there's no master limiter. Summing several tracks
  clips at 0 dBFS; render and check `max_volume` stays below 0 (aim ≈ −3 dB). Lower
  per-track `volume` (SetTrackParams) and synth `gain`.
- **SetTrackParams can't send 0.0 or turn mute/solo *off*** (proto3 default omission) —
  use OSC for those.
- **MIDI input** goes to the *selected* instrument track (or the first instrument if
  none selected); a virtual ALSA port "Gloopy MIDI In" is always available.
- **Commit and push frequently.** `origin = cave@cave.moxielogic.com:atgreen/gloopy.git`
  (self-hosted forge). One green, verified slice = one commit — then **`git push` to `origin`
  right away**; don't let local commits pile up. (Standing authorization for this repo: you
  don't need to ask before pushing here.) The docs-deploy workflow still targets GitHub Pages,
  not this forge.
- **`.gloopy` is XML.** To stand up a bus/group test topology fast, hand-edit the `MTRACK`
  `out`/`bus` attrs directly instead of scripting a dozen routing RPCs.

## Plugin hosting (VST3 + LV2)

Scanned list is cached at `~/.config/Gloopy/plugins.xml`; `ScanPlugins`/`ListPlugins`
expose it. Plugins are addressed by the identifier string from `ListPlugins`
(JUCE `createIdentifierString`). Saved projects store the plugin's own path, which for
VST3 is the bundle's inner `.so` — the loader needs the `.vst3` bundle, so on load we
re-match against the scanned list (`resolvePluginDescription`). Native editors embed
only for X11UI plugins (Surge, sfizz); others fall back to a generic panel.

### SFZ instrument — embedded sfizz (vendored)
`Source/SfizzGenerator.h` is a `Generator` backed by the **vendored sfizz library**
in `third_party/sfizz/` (BSD-2-Clause source, copied in — no submodule). It's built
as a static library by Gloopy's CMake (`add_subdirectory(third_party/sfizz)`, library
only: plugins/JACK/render off, dr_libs backend so no libsndfile LGPL, submodule-check
off since there's no `.git`). We call sfizz directly — `loadSfzFile(path)`,
`renderBlock`, `noteOn/Off`, `allSoundOff` — so there is **no plugin hosting and no
state hack**. Because sfizz disk-streams, it handles everything from one VPO section
to the full multi-GB Salamander piano in a small footprint (641 regions, ~80 MB RSS).

Load via the **`+ SFZ`** toolbar button, `AddSfzTrack` gRPC, or
`add_sfz_track`/`add-sfz-track` in the clients. Projects store just the `.sfz` path;
sfizz re-parses on load. Paths are resolved by `MainComponent::resolveSamplePath`:
absolute paths and a leading `~` are used as-is, and **relative** paths are searched
against `$GLOOPY_SAMPLE_PATH` (colon-separated), the project's directory, then `~/sfz`
— so the demo songs ship relative paths and stay portable. `SfizzGenerator::allNotesOff()` → `allSoundOff()`, which
actually stops notes (the sfizz *plugin* ignored CC120/CC123 — the embedded library
does not).

The first build compiles sfizz + Abseil (a few minutes, one-time; incremental builds
are unaffected). To re-vendor a newer sfizz: clone `sfztools/sfizz --recurse-submodules`
at the desired tag, copy it to `third_party/sfizz/`, and drop `.git`, `tests/`,
`benchmarks/`, and `external/simde/test/` (the bulk of the weight; keep `scripts/`
and `clients/` — the top-level CMake references them unconditionally). **Then re-apply
the `namespace Tunings` → `TuningsSfz` rename** in sfizz's `Tunings.h` / `Tunings.cpp` /
`Tuning.cpp`: sfizz ships an older strong-symbol `Tunings::` that ODR-clashes with
surge-common's newer inline one and **SIGSEGVs Surge at startup** when both are linked
(`GLOOPY_WITH_SURGE=ON`, the default). A fresh copy will silently clobber the rename.

> Historical note: earlier iterations hosted the sfizz **plugin** and injected the
> `.sfz` path by rewriting its saved state blob (`tools/sfizz-state.py`), and a
> later iteration hand-rolled a preloading SFZ parser. Both are superseded by the
> embedded library and have been removed.

## Sample libraries used by the demo songs (local, not in the repo)
- **VPO** (Virtual Playing Orchestra, orchestral SFZ) → `~/sfz/Virtual-Playing-Orchestra3/`.
  No piano — only celesta in Keys.
- **Salamander Grand Piano** (Ogg SFZ, ~74 MB) → `~/sfz/SalamanderGrandPianoV3_OggVorbis/`.
- `examples/demo-cinematic` and `demo-nocturne` reference these by absolute
  path (like the Surge demo needs Surge). The synthwave/lo-fi/ambient demos are portable
  (built-in synth + kit only).

## Authoring demo songs
Compose MIDI note-lists in Python, assemble live through the gRPC API (`NewProject` →
add/clear tracks → `AddSynthTrack`/`AddPluginTrack` + `AddClip` → effects → `SaveProject`),
then `RenderToFile` and check levels. Built-in synth params go straight through
`AddSynthTrack` (no baking). For sampled instruments, inject a retargeted sfizz state as
above. Keep projects MIDI-only (tiny) rather than embedding rendered audio.
