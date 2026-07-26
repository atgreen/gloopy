# Embedding Surge XT as Gloopy's default synth

**Status:** planned (opening slice). **Decided 2026-07-26.**
**Strategy:** embed the Surge XT synth *engine* into Gloopy's build (like the vendored
sfizz engine), and bundle a **curated ~150–300 patch** subset. Chosen over "host the
installed plugin" and "build the plugin from a submodule" — we want Gloopy to sound
great out of the box, self-contained, no external plugin install required.

## Licensing (the guardrail check — do NOT skip)

- **Surge XT is GPL-3.0** (`~/git/surge/LICENSE`). **Gloopy is AGPL-3.0-only**
  (`SPDX-License-Identifier: AGPL-3.0-only` in source headers).
- AGPLv3 and GPLv3 are **explicitly compatible to combine** (GPLv3 §13 / AGPLv3 §13,
  "Use with the GNU Affero GPL"). A combined/conveyed work is permitted; the vendored
  Surge portions remain GPL-3.0, Gloopy's own code stays AGPL-3.0, and the combined
  binary satisfies both. This is legally sound — but it is a real license entanglement:
  **the shipped Gloopy binary becomes a combined GPL/AGPL work.** Record this in
  `THIRD-PARTY-LICENSES.md` and keep Surge's `LICENSE` + `AUTHORS` in the vendored tree.
- **Factory content:** Surge's factory patches/wavetables ship under the repo's GPL-3
  (some 3rd-party patch packs have their own terms and live under
  `resources/data/patches_3rdparty` / `wavetables_3rdparty`). **Curate only from
  `patches_factory` + `wavetables` (first-party)** to keep the licensing clean; do not
  pull `*_3rdparty` without per-pack review.

## What Surge gives us (surveyed)

- Synth core is the CMake library **`surge::surge-common`** (`src/common/CMakeLists.txt`,
  target built from `SurgeSynthesizer` + `SurgeStorage` + the `sst-*` libs under `libs/`).
- Headless usage pattern (no JUCE UI) already exists in the test runner:
  - `createSurge(sr, loadAllPatches)` → `new SurgeSynthesizer(PluginLayer* parent, dataPath)`
    (`src/surge-testrunner/HeadlessUtils.cpp:33`).
  - `HeadlessPluginLayerProxy` is a minimal `PluginLayer` for headless embedding
    (`src/surge-testrunner/HeadlessPluginLayerProxy.h`).
  - The synth renders into `surge->output[2][BLOCK_SIZE]` (stereo, BLOCK_SIZE=32).
- Surge XT is **already installed** here (VST3+LV2) and Gloopy already hosts it — so the
  engine is known-good in this environment; the risk is purely in *embedding* it.
- `SurgeStorage` needs a **data path** at runtime (config + wavetables + patches). Our
  bundle ships a curated subset there.

## Architecture (mirror `SfizzGenerator`)

- New `Source/SurgeGenerator.h` : `class SurgeGenerator : public Generator`, holding a
  `SurgeSynthesizer` + a `HeadlessPluginLayerProxy`, pointed at Gloopy's bundled Surge
  data dir. Implement `render(juce::AudioBuffer<float>&, juce::MidiBuffer&, ...)` by
  dispatching MIDI to `playNote`/`releaseNote`/`process()` in BLOCK_SIZE chunks and
  copying `output` into the track buffer (the same chunked pattern `SfizzGenerator` uses
  for sfizz's fixed block size).
- **Default synth wiring:** `+ Synth` (and the empty-project/template default) creates a
  `SurgeGenerator` track instead of the built-in oscillator synth. Keep the old built-in
  synth available (it's tiny) but Surge becomes the default voice.
- **Patch loading:** `SurgeGenerator::loadPatch(path)` → `synth->loadPatchByPath(...)`;
  expose patches through the existing preset/browser system (a "Surge" preset category /
  a Presets browser tab — ties into Wave 6 browser #1 Presets tab and Wave-1 preset work).

## Vendoring mechanism

- **Do NOT copy Surge's 1.2 GB tree into the repo.** Add Surge as a **git submodule**
  under `third_party/surge` pinned to a known-good commit (mirrors how sfizz is embedded,
  but by-reference given the size). `.gitmodules` gets its first entry.
- CMake: `add_subdirectory(third_party/surge/... )` to build `surge::surge-common` (or a
  reduced target list — Surge's CMake is large; scope down to the synth core, no UI/plugin
  wrappers), `target_link_libraries(Gloopy PRIVATE surge::surge-common)`.
- **Curated data bundle:** a build/install step copies only the curated patch subset
  (~150–300 `.fxp` from `patches_factory`) + *only the wavetables those patches reference*
  + Surge's required config into `third_party/surge-data/` (a few MB), installed next to
  the binary and pointed to as the SurgeStorage data path (via `GLOOPY_ASSETS_DIR` sibling,
  like the Salamander piano). Curation script lives in `scripts/`.

## Sliced plan (each a grind slice: probe → implement → prove → commit)

1. **Isolation probe (NEXT).** Build `surge::surge-common` alone via Surge's CMake in a
   scratch dir; write a tiny headless program (`createSurge` → load one factory patch →
   `process()` a few blocks → assert non-silent RMS on `output`). Proves the engine
   embeds + renders headless here, and measures build cost. No Gloopy changes yet.
2. **Submodule + CMake link.** Add the `third_party/surge` submodule; get Gloopy to
   configure+build+link `surge-common` (scoped to the core; exclude UI). Green build only.
3. **`SurgeGenerator` + one hardcoded patch.** Mirror `SfizzGenerator`; a synth track can
   be a SurgeGenerator loaded with one bundled patch; smoke-render proves non-silent audio
   into the mix and a composition round-trip.
4. **Curated patch bundle + curation script.** Select ~150–300 first-party patches + the
   wavetables they need; install into the data dir; document the selection.
5. **Default-synth wiring + preset browser.** `+ Synth` defaults to Surge; patches
   browsable/loadable from the desktop (Presets browser tab) — the required desktop control.
6. **THIRD-PARTY-LICENSES.md + docs.** Record the GPL-3 vendored engine + AGPL/GPL combined
   work note; update README/ROADMAP.

## Risks / open questions

- **Build weight:** Surge's CMake pulls many `sst-*` sublibs; scoping to just the synth
  core (no UI, no plugin wrapper) is essential to keep Gloopy's build sane. If it's still
  too heavy, fall back to "build the Surge XT plugin from the submodule and host it" (the
  option not chosen) — revisit only if the embed build proves impractical.
- **Data-path/config:** confirm the minimal set of files `SurgeStorage` requires to init
  (configuration + at least the wavetables referenced by bundled patches) during slice 1.
- **BLOCK_SIZE / SR:** Surge fixes BLOCK_SIZE=32; the `SfizzGenerator` chunking pattern
  handles a fixed engine block inside Gloopy's variable callback — reuse it.

## Build-state findings (2026-07-26) — resume slice 1 here

- No existing Surge build tree in `~/git/surge` (no `build/`), though Surge XT is
  installed (`~/.vst3`, `~/.lv2`) — provenance of that install unconfirmed (may be a
  distro package, not this repo).
- `~/git/surge/libs/` **directories are populated** (JUCE, sst/*, fmt, PEGTL, simde,
  r8brain, luajitlib, oddsound-mts, …) BUT `git submodule status` shows a `-` prefix
  (reports uninitialized). **First probe action:** verify whether `libs/*` actually have
  content (e.g. `ls libs/JUCE/modules`, `ls libs/sst/sst-basic-blocks/include`); if empty,
  `git submodule update --init --recursive` (large — pulls JUCE etc.). If populated, the
  `-` is cosmetic and we can configure directly.
- **Next concrete step:** in a scratch dir, `cmake` Surge scoped to the `surge-common`
  target only (avoid the UI/plugin/`surge-xt` targets), build it, then a ~40-line headless
  main (`new SurgeSynthesizer(&HeadlessPluginLayerProxy, dataPath)` → `loadPatchByPath` a
  factory patch → `process()` ~100 blocks → assert RMS(`output`) > 0). Time-box; if the
  configure needs network or the build is > ~15 min, checkpoint and consider the fallback
  (build the plugin + host it) noted in Risks. Keep this OUT of Gloopy's tree until green.
