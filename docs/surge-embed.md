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

1. **Isolation probe — ✅ DONE (2026-07-26).** Built `surge-common` headless (no JUCE)
   and linked `docs/surge/probe.cpp` against it: it constructed a `SurgeSynthesizer` on a
   data path, **scanned 3,559 factory patches / 417 categories**, played middle C, and
   rendered **non-silent** stereo audio (**RMS 0.070**, blockSize 32). The engine + the
   embed contract are proven. **Reproducible recipe below.**
2. **CMake link — ✅ DONE (2026-07-26, CMake integration).** Gloopy's CMakeLists gained a
   `GLOOPY_WITH_SURGE` option (default ON) + `GLOOPY_SURGE_DIR` (defaults to the
   `third_party/surge` submodule). When on, it sets the scope flags and
   `add_subdirectory`s Surge, linking `surge::surge-common`. Proven: Gloopy compiles all 92
   surge-common objects inside its build tree and links (20 MB binary, exit 0); smoke suite
   still green (no regression); and `GLOOPY_WITH_SURGE=OFF` still configures a lean,
   pure-AGPL/C++17 build. **Embedding gotcha fixed:** surge-common's `src/common/CMakeLists.txt`
   references r8brain via `${CMAKE_SOURCE_DIR}` (= the *top-level* project = Gloopy when
   embedded), which mis-resolves. A configure-time `sed` shim (mirroring Gloopy's existing
   JUCE LV2 sed) redirects those 2 lines to `${SURGE_SOURCE_DIR}` (Surge's own root, correct
   under add_subdirectory). Confirmed surge-common does NOT propagate its C++20 standard, so
   Gloopy stays C++17 (only slice-3's SurgeGenerator TU needs per-file C++20 + -fno-char8_t).
   **Remaining for this slice (2b):** actually add the `third_party/surge` git submodule so a
   default checkout has the source (today `GLOOPY_SURGE_DIR` is pointed at `~/git/surge` for
   the build; without a source tree the CMake warns + builds Surge-less). The sed shim will
   then apply to the submodule — carry it as a documented compat patch.
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

## Proven build recipe (slice 1 result — the basis for slice 2's CMake link)

The exact configuration that builds `surge-common` headless and links a consumer here:

- **Submodules `surge-common` needs** (init only these — NOT `libs/JUCE`, which is
  ~500 MB and only for the plugin/UI): `simde fmt luajitlib pffft airwindows binn
  eurorack sst oddsound-mts tuning-library PEGTL zstd r8brain-free-src`
  (`git submodule update --init --recursive -- libs/<each>`). `sqlite-3.23.3` is
  already vendored source (not a submodule).
- **CMake flags** (scope to the synth core; no JUCE / plugins / tests):
  `-DSURGE_SKIP_JUCE_FOR_RACK=TRUE -DSURGE_SKIP_VST3=TRUE -DSURGE_SKIP_STANDALONE=TRUE
  -DSURGE_BUILD_XT=OFF -DSURGE_BUILD_FX=OFF -DSURGE_BUILD_CLAP=OFF -DSURGE_BUILD_LV2=OFF
  -DSURGE_BUILD_TESTRUNNER=OFF -DBUILD_TESTING=OFF -DENABLE_LTO=OFF`
  **and `-DSURGE_SKIP_WERROR=TRUE`** — required: vendored sqlite 3.23.3 trips `-Werror`
  under GCC 16.
- **Consumer compile flags:** `-std=c++20 -fno-char8_t` are **mandatory** — Surge's
  `sst-effects` headers fail to parse under c++17, and Surge builds with `-fno-char8_t`.
  The `surge-common` target exports its source dir + `SURGE_COMPILE_BLOCK_SIZE=32`
  **PUBLIC**, so a consumer that links it gets `#include "SurgeSynthesizer.h"` + `BLOCK_SIZE`
  for free. (Gloopy's build is C++17 today — this bumps the Surge-touching TUs / the
  whole target to C++20; confirm JUCE is happy or isolate SurgeGenerator's TU.)
- **Gotchas hit:** (a) `ninja surge-common` compiles surge-common's 84 objects into
  `CMakeFiles/surge-common.dir/` but does **not** archive them into a single `.a`, and
  does **not** build the `sqlite`/dep archives — a consumer target that *links*
  `surge::surge-common` pulls them via CMake, but manual linking must gather the loose
  objects + `find … -name '*.a'` + build `libsqlite.a` explicitly. In Gloopy (slice 2) we
  use `add_subdirectory` + `target_link_libraries(Gloopy PRIVATE surge::surge-common)`,
  which handles all of this. (b) Link with `-Wl,--start-group … --end-group` (inter-lib
  cycles). (c) Data path = `~/git/surge/resources/data` for the probe; Gloopy will bundle
  a curated subset (slice 4).
- **Headless API** (probe, mirrors `HeadlessUtils`): `new SurgeSynthesizer(&layerProxy,
  dataPath)` → `setSamplerate` → `playNote(0,60,100,0)` → `process()` fills
  `output[2][BLOCK_SIZE]` → `releaseNote(0,60,0)`. `layerProxy` = a 2-method
  `SurgeSynthesizer::PluginLayer` subclass. `storage.patch_list` / `patch_category` are
  public. See `docs/surge/probe.cpp`.

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
