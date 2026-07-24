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

## Plugin hosting (VST3 + LV2)

Scanned list is cached at `~/.config/Gloopy/plugins.xml`; `ScanPlugins`/`ListPlugins`
expose it. Plugins are addressed by the identifier string from `ListPlugins`
(JUCE `createIdentifierString`). Saved projects store the plugin's own path, which for
VST3 is the bundle's inner `.so` — the loader needs the `.vst3` bundle, so on load we
re-match against the scanned list (`resolvePluginDescription`). Native editors embed
only for X11UI plugins (Surge, sfizz); others fall back to a generic panel.

### sfizz (SFZ sampler) — installed without sudo
Not in Fedora repos and no prebuilt Linux binary. We pulled the **Audinux COPR** rpm
(matches Fedora's ABI) and extracted it into `~/.vst3` / `~/.lv2` with no install:
```sh
# from the copr repodata, download sfizz-*.fcNN.x86_64.rpm + vst3-sfizz-ui-*, lv2-sfizz-ui-*
rpm2cpio pkg.rpm | cpio -idm            # then cp usr/lib64/{vst3/sfizz.vst3,lv2/sfizz.lv2} to ~/
```
`sfizz_render` (CLI, ships in the base rpm) renders `.sfz` + MIDI → WAV; handy for
auditioning a library outside Gloopy.

### Loading a specific SFZ into a hosted sfizz — headless
sfizz has **no parameter for the .sfz path**; the editor's file chooser is the only UI
way. But the loaded path lives as plaintext in the saved plugin state. So:
`tools/sfizz-state.py` decodes/re-encodes that state and retargets the path to any SFZ
(`tools/sfizz-reference-state.txt` is a working state to retarget from). Build a project
with a sfizz track via `AddPluginTrack`, `SaveProject`, then substitute the track's
`<PLUGIN ... pstate="...">` with `retarget(reference, "/abs/path.sfz")`. No GUI needed.

## Sample libraries used by the demo songs (local, not in the repo)
- **VPO** (Virtual Playing Orchestra, orchestral SFZ) → `~/sfz/Virtual-Playing-Orchestra3/`.
  No piano — only celesta in Keys.
- **Salamander Grand Piano** (Ogg SFZ, ~74 MB) → `~/sfz/SalamanderGrandPianoV3_OggVorbis/`.
- `examples/demo-cinematic.gloopy` and `demo-nocturne.gloopy` reference these by absolute
  path (like the Surge demo needs Surge). The synthwave/lo-fi/ambient demos are portable
  (built-in synth + kit only).

## Authoring demo songs
Compose MIDI note-lists in Python, assemble live through the gRPC API (`NewProject` →
add/clear tracks → `AddSynthTrack`/`AddPluginTrack` + `AddClip` → effects → `SaveProject`),
then `RenderToFile` and check levels. Built-in synth params go straight through
`AddSynthTrack` (no baking). For sampled instruments, inject a retargeted sfizz state as
above. Keep projects MIDI-only (tiny) rather than embedding rendered audio.
