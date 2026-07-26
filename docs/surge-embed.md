# Embedding Surge XT as Gloopy's default synth

## ⚠️ DIRECTION CHANGE (2026-07-26): ship the Surge XT *plugin*, host it for the real UI

User decision (superseding the headless-core-only plan below): **build the full Surge XT
LV2 plugin from the `third_party/surge` submodule, bundle/ship it, and host it** as Gloopy's
default synth — so users get **the real Surge XT editor** (via Gloopy's existing
`openPluginEditor`, which already hosts VST3/LV2 with native UIs). The headless embed
(surge-common, slices 1–6) has no UI; that gap is what prompted this pivot.

**Plan (multi-session):**
1. Build the Surge XT **LV2** plugin from source — `scripts/build-surge-plugin.sh` (init
   JUCE + configure LV2 + build + stage `third_party/surge-plugin/Surge XT.lv2`).
2. Ship the `.lv2` + Surge's data dir in the package; add the bundle dir to Gloopy's plugin
   scan path so the bundled Surge XT is found without a system install.
3. Make the default synth (`+ Synth → Surge XT`) + the **Presets tab** instantiate the
   *hosted bundled plugin* (real UI) and load `.fxp` into it, instead of the headless
   `SurgeGenerator`.
4. Retire (or keep as a lean fallback) the headless embed once the plugin path works.
5. CI/packaging: the release build compiles the plugin (install GUI deps in the container).

**✅ PROGRESS (2026-07-26, commit 8987ce3 — the earlier local-build blocker is resolved):**
- **Step 1 DONE.** `scripts/build-surge-plugin.sh` built the full Surge XT LV2 plugin and staged
  `third_party/surge-plugin/Surge XT.lv2` (37 MB `.so` + the three `.ttl` files). The GUI deps
  are now installed on this box, so it builds & verifies here.
- **Step 1 (scan+host) DONE + a real bug fixed.** `PluginHost` adds the bundle dir to the scan
  path *and* — the subtle fix — prepends it to `$LV2_PATH` before the LV2 format is constructed.
  JUCE builds its lilv world once from `getDefaultLocationsToSearch()` and instantiates *cached*
  plugins from that world; adding the dir only to the scan path made Surge discoverable but made
  `create()` fail from the fast cached-startup path ("Unable to locate plugin with the requested
  URI"). Verified in an isolated `HOME` (no `~/.lv2`): fresh scan **and** cached create both
  instantiate Surge XT. The editor UI renders from Surge's cmrc-embedded skin, so the hosted
  plugin works with no external install.
- **Step 5 DONE.** The 37 MB bundle is gitignored + built in CI: `release.yaml` runs
  `build-surge-plugin.sh` before configure (so the `if(EXISTS …)` install rule fires) and installs
  the GL/Xrender dev deps the GUI build needs.

**⏳ Remaining follow-ups:**
- **Step 2b — factory patch/wavetable library — ✅ DONE.** `build-surge-plugin.sh` now stages
  Surge's first-party `resources/data` (excluding the `*_3rdparty` packs) as
  `third_party/surge-plugin/SurgeXTData/` (~62 MB, 639 factory patches). Surge XT finds it via
  **portable mode** — at construction it walks UP from its own `.so` dir looking for a
  `SurgeXTData/` dir (`SurgeStorage.cpp`), so installing to `bin/plugins/SurgeXTData` (beside the
  `.lv2`) is auto-found with NO env var / NO Gloopy code / NO CMake change (the existing
  `install(DIRECTORY surge-plugin/)` rule ships it; gitignore already covers it). Verified by
  `strace` in an isolated `HOME` (no `~/.lv2`, no system Surge): **558 opens under the bundled
  `SurgeXTData`** — wavetables + all factory patch categories load. (`configuration.xml` is NOT in
  the data dir — Surge compiles it in as a binary resource.)
- **Licensing — ✅ DONE.** `THIRD-PARTY-LICENSES.md` now documents Surge XT in both forms
  (embedded `surge-common` core *and* the bundled GPL-3 LV2 plugin `.so` shipped in the RPM/DEB
  packages), the combined-work / source-availability obligation covering the shipped plugin, and
  that JUCE is statically linked into it under GPL terms.
- **Step 3 — the `+ Synth` toolbar default is DONE.** `+ Synth` no longer pops a 3-item menu
  (Basic synth / embedded Surge / full editor); it now directly adds a **Surge XT (full editor)**
  track — Gloopy's default instrument — hosting the installed/bundled plugin (LV2 preferred).
  Non-breaking: serialization, `apiAddSynthTrack`, tests, and examples are untouched (they use the
  API, not the UI lambda); other instrument types keep their own toolbar buttons (+SFZ/+Sample/
  +Audio/+Plugin). **Remaining in step 3:** the *Presets* browser tab still drives the headless
  `SurgeGenerator` / embedded core — point it at the hosted plugin and load `.fxp` into it.
- **Removals + migration (do LAST, after the above).** User directive: **full removal + migrate
  everything to Surge** ([[surge-is-the-synth]], confirmed 2026-07-26 with the downsides accepted).
  Migrate both templates (Piano+Bass+Drums, Lead+Bass) + all **23 example synth tracks** off
  `SynthGenerator` to hosted Surge XT plugin tracks with a hand-picked factory patch per voice,
  THEN remove `SynthGenerator` entirely (code/proto/python/tests/`type="synth"` format) and the
  embedded Surge (`SurgeGenerator` + `GLOOPY_WITH_SURGE` + `surge-common` link). Consequences
  accepted: all melodic content becomes Surge-bundle-dependent; old `.gloopy` files with synth
  tracks stop loading. Scope facts: templates use `SynthGenerator` for Lead/Bass/fallback-piano
  only (drums = procedural `DrumSynth`+Sampler, real piano = Salamander SFZ — both survive);
  no SFZ lead/bass content exists in-repo.

  **LINCHPIN — load a named factory patch into a hosted Surge XT LV2 track (research 2026-07-26,
  NOT yet implemented; resume here).** So a migrated Bass sounds like a bass, each track needs a
  specific patch, then Gloopy bakes it via the hosted plugin's serialized state. Mechanisms in
  `third_party/surge/src/surge-xt/SurgeSynthProcessor.cpp` + `.../common/SurgeSynthesizerIO.cpp`:
  - **Program API** (`getNumPrograms`/`setCurrentProgram`/`getProgramName`) — gated on
    `SURGE_EXPOSE_PRESETS` (OFF in our build; option in `surge/src/CMakeLists.txt:9`). Even if
    enabled it drives JUCE's *program* API, which almost certainly does NOT cross the LV2 host
    boundary (LV2 has no program concept). Treat as a dead end for LV2. **Do not chase.**
  - **State path (RECOMMENDED)** — `AudioProcessor::setStateInformation(bytes)` →
    `enqueuePatchForLoad` → `loadRaw(chunk)` (SurgeSynthesizerIO.cpp:419/461). JUCE maps
    get/setStateInformation to the LV2 `state:` extension, so it DOES cross the LV2 boundary.
    Catch: `loadRaw` expects Surge's **raw chunk**, but the on-disk factory `.fxp` files are
    **FXP-wrapped** (`fxChunkSetCustom` header: magic `CcnK`/`FPCh`/`cjs3`, checked at
    SurgeSynthesizerIO.cpp:264). So the host must read a factory `.fxp`, **strip the
    `fxChunkSetCustom` header**, and pass the trailing chunk (`chunkSize` at line 299) to
    `setStateInformation`. Next step: a headless probe — instantiate the hosted Surge XT,
    load a known factory `.fxp` (e.g. a bass patch) this way, dump `--plugparams`, and confirm
    params differ from INIT. Then wrap as `apiAddSurgePluginTrackWithPatch(patchPath)` and
    serialize the resulting plugin state per track.
  - Alt to probe if state path is fragile: `loadPatchByPath(fxpPath)` (SurgeSynthesizerIO.cpp:242)
    / the `patchid_file` field (SurgeSynthProcessor.cpp:1726) — a file-path loader; find its
    host-facing trigger (OSC? a control?).

Everything in the headless-embed sections below still works and is committed.

---


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

- **DECIDED (2026-07-26, user): git submodule** for the *source* (the two options were a
  pruned source copy vs. a submodule vs. external-only; user chose submodule). Add Surge as a
  **git submodule** under `third_party/surge` pinned to the known-good commit
  `9e73f42cc8908ebb7e1ff8a62a1c7d474655568e` (upstream `https://github.com/surge-synthesizer/surge.git`).
  `.gitmodules` gets its first entry.
  - **Caveat to handle in 2b:** a naive `git clone --recursive` of Gloopy would pull Surge's
    *own* submodules recursively, incl. `libs/JUCE` (~500 MB) which surge-common doesn't need.
    Ship a `scripts/init-surge.sh` that inits ONLY the needed sub-submodules (the slice-1 list:
    simde fmt luajitlib pffft airwindows binn eurorack sst oddsound-mts tuning-library PEGTL
    zstd r8brain-free-src) and document "don't use --recursive; run the script." The Surge
    *source* submodule is for building surge-common only — its `resources/data` is NOT used at
    runtime (the factory content ships separately in `third_party/surge-data`, slice 4).
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
   **2b — ✅ DONE (2026-07-26):** added the `third_party/surge` git submodule (URL
   `https://github.com/surge-synthesizer/surge.git`, pinned `9e73f42c`), so the committed
   default `GLOOPY_SURGE_DIR=third_party/surge` resolves. Added by `--reference ~/git/surge`
   so Gloopy's `.git` stays tiny (surge `.git` is 814 MB); `.gitmodules` has `ignore = dirty`
   (the r8brain sed shim modifies the submodule tree at configure). `scripts/init-surge.sh`
   inits ONLY the sub-submodules surge-common needs (JUCE deliberately skipped — verified 0
   entries), optionally borrowing objects from `$GLOOPY_SURGE_REFERENCE`/`~/git/surge`. Verified
   a clean configure+build against `third_party/surge` (Surge ENABLED). README documents the
   `init-surge.sh` (don't `--recursive`) workflow.
3. **`SurgeGenerator` — ✅ DONE (2026-07-26).** A Surge track renders non-silent audio
   through Gloopy's mix (smoke: peak ~0.33). **The crash was an ODR namespace clash:**
   the vendored **sfizz** ships its *own older* copy of the `Tunings::` tuning-library
   (`third_party/sfizz/src/external/tunings/`, non-inline → a **strong** global
   `Tunings::readSCLStream`), while surge-common vendors a *newer inline* version (weak
   symbols). The linker kept sfizz's strong symbol, so surge-common's `SurgeStorage` ctor
   called sfizz's incompatible-layout `Tunings::` implementation → SIGSEGV in
   `evenTemperament12NoteScale`. The standalone probe had no sfizz, so it never clashed.
   **Fix:** renamed sfizz's private copy `namespace Tunings` → `namespace TuningsSfz` (3
   files, ~30 refs; `third_party/sfizz` is Gloopy's own copied tree, filename/include
   guards preserved) so the two libraries no longer share a symbol. Verified: a Surge track
   renders non-silent both with `$GLOOPY_SURGE_DATA` set (factory patches) and without (the
   cmrc-embedded default init patch still sounds) — and no longer crashes either way. The
   original scaffold below stands. (Diagnosis lesson for Wave 7 #27: ASan would have flagged
   this instantly — but `nm -C | grep Tunings::` showing a duplicate strong symbol nailed it.)

   Scaffold: built
   `Source/SurgeGenerator.{h,cpp}` (PIMPL: C++17 header, C++20 `.cpp` — the only Surge-header
   TU, compiled `-std=c++20 -fno-char8_t -w` per CMake; a fixed-32-block carry buffer adapts
   Surge's block to Gloopy's), plus `apiAddSurgeTrack` + `AddSurgeTrack` RPC + Python
   `add_surge_track` + a (gated) smoke block. Compiles + links clean. **But calling
   AddSurgeTrack CRASHES the process** (SIGSEGV, core-dumped).

   **Precise crash (from coredumpctl):**
   `Tunings::readSCLStream` ← `Tunings::evenTemperament12NoteScale()` ← `SurgeStorage::SurgeStorage`
   ← `SurgeSynthesizer::SurgeSynthesizer` ← `SurgeGenerator::prepare` ← `apiAddSurgeTrack`.
   It's in the SurgeStorage *constructor's* tuning init (`SurgeStorage.cpp:414`,
   `twelveToneStandardMapping = Tunings::Tuning(evenTemperament12NoteScale(), …)`), a
   **completely self-contained** call — parses a hardcoded 12-TET Scala string, no file/data
   dir/cmrc involved. Confirmed it's construction, not render: with render forced to a no-op
   the crash is unchanged. The **standalone probe (docs/surge/probe.cpp) does the identical
   construct fine** — so it's specific to surge-common being linked into the Gloopy binary.
   Not an ODR clash with Gloopy's own `.scl` code (Gloopy has a hand-rolled parser in
   Scales.cpp, no `Tunings::` symbols). Not a data-thread race (audio callback + toValueTree
   are both under `engineLock`; the earlier "crash in toValueTree" cores were heap corruption
   surfacing later — same underlying bug).

   **Leading hypothesis:** ODR/ABI mismatch in the header-heavy **`Tunings::` tuning-library**
   (`libs/tuning-library`, largely inline) between the C++20 `-fno-char8_t` surge-common build
   and how it's linked into Gloopy — or a static-init-order issue. **Next step:** build
   SurgeGenerator + surge-common under **ASan/UBSan** (ties into Wave 7 #27) to catch the exact
   corruption; if ODR, check whether any `Tunings::`/inline symbol is defined in two TUs with
   different flags, and consider building the tuning-library with hidden visibility or matching
   the consumer's standard. Repro: build with `-DGLOOPY_SURGE_DIR=~/git/surge`, set
   `GLOOPY_SURGE_DATA=~/git/surge/resources/data`, `grpcurl … AddSurgeTrack`. Smoke block is
   gated behind `GLOOPY_SMOKE_SURGE=1` until this is fixed.
4. **Factory data bundle — ✅ DONE (2026-07-26).** Per the user's choice, bundle **ALL 639
   first-party factory patches + all first-party wavetables** (`patches_factory` 24 MB +
   `wavetables` 6.7 MB ≈ 31 MB) into `third_party/surge-data/` — NOT the `*_3rdparty` packs.
   `SurgeGenerator::dataDir()` resolves `$GLOOPY_SURGE_DATA` → `GLOOPY_ASSETS_DIR/surge-data`
   (dev tree) → `<exeDir>/assets/surge-data` (installed, via the new install rule). Verified:
   the Presets tab populates + a Surge track renders **without** `$GLOOPY_SURGE_DATA` (screenshot
   + smoke). Vendoring decision: **git submodule** for the *source* (see #2b) — kept separate from
   this data copy (source builds surge-common; this dir is runtime content; no duplication in use).
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
