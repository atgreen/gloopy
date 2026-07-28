---
name: gloop-grind
version: 1.0.0
description: |
  The long-march roadmap for GLOOPY — the compact JUCE 8 / C++17 linear-arranger
  DAW at ~/git/gloopy (plugin hosting, composition-as-repo projects, OSC + gRPC
  control, automation, embedded sfizz, scriptable/headless workflows). Invoke when
  the user says "grind", "keep going on Gloopy", "what's next", "pick the next
  feature", or wants to continue the roadmap across sessions. Loads the north star,
  the principles that judge each slice, the current shipped state, the working
  discipline (build/test/commit/verify-headless), and the full ordered backlog of
  features we have actually committed to building. NOTE: this is the *Gloopy* grind
  (project-local). A separate global `grind` skill belongs to the Plume Lisp project
  — don't confuse them.
allowed-tools:
  - Bash
  - Read
  - Write
  - Edit
  - Glob
  - Grep
  - Agent
---

# grind — the march to make Gloopy the scriptable, composition-as-repo DAW

This skill is the durable map and process for **Gloopy** (`~/git/gloopy`): a compact
JUCE 8 / C++17 linear-arranger DAW that hosts VST3/LV2 plugins and an embedded sfizz
SFZ engine, and that is driven end-to-end by a **control API** (OSC UDP 9000 + gRPC
`127.0.0.1:50051`) over projects stored as a **diff-friendly composition-as-repo**
text format.

Three living documents, in order of authority:
- **`AGENTS.md`** (repo root) — the hard-won landmines: proto3-omits-zero, one
  instance at a time (`pkill -x Gloopy`), `NewProject` resets to the 5-track kit,
  manual level staging, the sfizz vendoring, sample-path resolution. Read it every
  session; it is the operational ground truth.
- **`docs/ROADMAP.md`** + **`ideas.md`** — the *design record* of what shipped
  (Phases 1–4, composition, recording, presets) and the *raw idea pool* (14 Feature
  Ideas + 16 Ardour borrowings). This skill is the curated subset of that pool that
  we will actually build, in order. `ideas.md` is the superset; **this file is the
  commitment.**
- **This skill** — the *shape* of the journey and *how* to walk it. Stable. The
  volatile per-feature detail lives in `docs/ROADMAP.md` and the git log.

## The north star (what Gloopy is trying to be)

**The DAW you can drive from a script and store in git.** Not a full DAW with an API
bolted on — a DAW whose *primary* surface is the composition-as-repo text format and
the OSC/gRPC control API, with the GUI as one client among Python, Common Lisp, CI,
and future AI agents. Every capability is reachable *and verifiable* headlessly:
`grpcurl` builds it, `--render` bounces it, a script asserts the WAV. Gloopy stays
**smaller, more scriptable, and more composition-repo friendly** than a typical DAW —
that constraint is the product, not a limitation.

## The principles (how every slice is judged)

1. **API-first, GUI-second — but GUI is not optional.** A feature isn't done until it's
   reachable over gRPC (and OSC where it's a live knob) with a stable id. The UI is a
   client of that surface, built after — and never the only way to do the thing. If it
   can't be scripted, it isn't Gloopy-shaped. **AND, per explicit user direction
   (2026-07): everything we add must have a desktop UI where appropriate — a control,
   menu item, mouse gesture, or shortcut a desktop user can actually reach.** A slice is
   NOT done at "backend + gRPC + smoke green"; it also needs the desktop affordance,
   wiring the same shared transform, screenshot-validated on Xvfb. Do not ship API-only
   features (the chord tool was caught being grpcurl-only). "Where appropriate" excludes
   pure non-visual plumbing (format internals, scan/validate CLI).
2. **Everything lives in the composition text format.** New state serialises to
   readable TOML / `.notes` / `.points` under the content-addressed dirty-write model
   (`Source/Composition.cpp`), so it diffs and travels. No new hidden project blobs. A
   `dir → runtime → dir` round-trip must stay byte-stable.
3. **Every slice is headless-verifiable.** Prove it with `grpcurl` + `--render` + a
   WAV/text assertion, wired into `tests/smoke.sh` where it fits. "I built the UI, it
   looks right" is not proof. Favour work whose correctness a script can see.
4. **The audio thread is sacred.** No allocation, locks, file I/O, or blocking in
   `renderBlock`. New realtime data reaches the audio thread lock-free or under the
   `engineLock` try-lock; structural edits go on the message thread
   (`callOnMessageThread`, `pushUndoSnapshot()` first). Build the object off-thread,
   then swap it in under the lock — the presets/instrument-swap pattern.
5. **Stay curated, not comprehensive.** Gloopy hosts VST3/LV2, so built-ins are a
   small high-value set, not a catalog. Borrow *systems and workflows* from bigger
   DAWs (Ardour, trackers) — never their code or assets without a license review
   against Gloopy's **AGPL-3.0** (Ardour is GPL; direct copying needs review). Keep the
   JUCE layout simple; don't grow a window-heavy workspace or a per-device C++
   subsystem for every controller — keep mappings data-driven. Don't embed a second
   scripting VM (no Lua): scripts drive the existing gRPC API. **Never name other
   named DAWs/trackers as the source in code, docs, or commits — borrow the idea, not
   the brand.**

## Current state (verify against git log + docs/ROADMAP.md each session)

`[x]` done · `[~]` partial · `[ ]` not started

- `[x]` **Phases 1–4 (docs/ROADMAP.md):** MIDI recording into clips, master limiter +
  clip meters, mixer-insert control API, MIDI hot-plug, undo/redo (snapshot-based),
  parameter **automation** (breakpoint lanes, `SetAutomation`/`GetAutomation`),
  structural **change events** on `Subscribe`, **region/stem render**
  (`RenderToFile` + range/target), Python client + CL client, expanded
  **dual-osc + resonant filter + LFO synth**, groove/swing + piano-roll velocity
  editing, unit tests (`GloopyTests`, ctest) + smoke test + CI under xvfb.
- `[x]` **Composition-as-repo format** (`Source/Composition.cpp`,
  `gloop-compositions.md`): TOML manifests + `.notes`/`.points` + WAV/plugin
  sidecars; content-addressed dirty writes; zip read; `.gloopy` / folder / `.zip`
  all first-class in the File menu; `SaveComposition`/`LoadComposition` RPCs.
- `[x]` **Audio recording** (`Source/Recording.cpp`, `recording.md`): live-input
  capture to a WAV take via `ThreadedWriter`; *referencing* audio clips
  (`Clip.audioFile`/`takeId`); monitoring, count-in, punch, take lanes / take
  management, loop recording; `GLOOPY_REC_TEST_TONE_HZ` test seam; `SetLoop` RPC.
- `[x]` **Embedded sfizz** (`Source/SfizzGenerator.h`, `third_party/sfizz/`, BSD-2,
  vendored no-submodule): native `loadSfzFile`/`renderBlock`, disk-streaming, portable
  sample-path resolution, freewheeling for offline bounce.
- `[~]` **Presets** (`Source/Presets.cpp`): synth, effect-chain, and general
  instrument (synth **or** SFZ, cross-type) presets as composition-friendly TOML under
  `<userAppData>/Gloopy/presets/` (`GLOOPY_PRESET_PATH` override); `ListPresets` +
  save/load RPCs + Python client. **Remaining:** sampler + plugin instrument presets,
  plugin effects inside effect-chain presets, and all preset UI (menus, browser,
  New-From-Template). These fold into Wave 6 items #16 below.
- `[~]` **Session view (clip-launch grid)** (`Source/SessionModel.h` / `SessionLauncher.h`
  / `SessionView.h`, `docs/session-view.md`): the Ableton-style Session view **alongside**
  the Arrangement view — **Tab cycles Arrange → Session → Mixer**. Sample-accurate launch
  engine (block-split, per-track arrangement override, one clip per track, global scene rows,
  launch quant), grid UI with scene/master column **pinned left as a frozen pane**, record
  live MIDI into slots, and **session → arrangement** capture/copy; a bottom **Device View**
  edits the selected track's chain. Slices 1–4 (model+persistence, launch engine, UI+Tab,
  recording) landed. **Remaining:** control-API (OSC/gRPC launch/stop/query) + polish
  (follow-actions, capture, quantize menu). See the Session View epic in the backlog.
- `[x]` **Group / submix routing — the grouping epic (complete).** A **main-output** route
  (`MixerTrack::output`, 0 = master) sends a strip's *whole* signal into a bus — a submix /
  Ableton "group" — distinct from a `Send` (parallel post-FX copy). `apiSetInsertOutput` /
  `apiGroupInserts` / `apiGatherGroup` / `apiUngroup`; foldable **session group columns**
  (nested groups, depth indent, fold/unfold-all), **GROUP vs RETURN** header tags, interactive
  group strip (fader/pan/mute/solo/FX), settable group colour, and **transitive solo**. Group
  gesture from either view: **Cmd+G** group / **Cmd+Shift+G** ungroup / **Cmd+Shift+F**
  fold-all. Load-bearing invariant: a bus must be a higher `mixerTracks` index than everything
  routing into it (see the group-bus memory note).
- `[x]` **Mixer channel model — dynamic per-track inserts.** `mixerTracks` = `[Master] + one
  insert per track + buses` (the fixed 8-strip "Ins N" pool is gone; `insertMixerTrackAt` /
  `removeMixerTrackAt` reindex the whole routing space). Strips backing a track show the
  **track's name + colour** (rename syncs the track); **Delete track** on the header menu; and
  **detachable device windows** (a floating `DevicePanel` per insert, pinned to a stable
  `MixerTrack*`, so several effect chains are visible at once — the Reaper/Ardour model).
- `[x]` **UI: elevation-over-borders pass.** Surfaces separate by the `Palette` shade ladder +
  spacing, not outlines; borderless button fills; one `Palette::radius`; thin meters; `lineSoft`
  panel seams; **Play is the hero** in the transport; watermark empty states. Keep the calm flat
  identity — no drop shadows, uppercase section headers. (See the ui-elevation memory note.)
- `[~]` **Surge XT front (featured synth) — DONE/paused.** The **hosted Surge XT LV2 plugin**
  is the featured "+ Synth" default (full editor via the plugin path) with **bundled first-party
  factory patches** (Presets browser tab, ~639 patches). The embedded Surge core **and** the
  simple `SynthGenerator` are **kept** (removal/migration is **paused**): baking a factory `.fxp`
  into a *hosted* plugin via the host state API is a **proven hard blocker**, so the embedded core
  stays as the one path where patch-by-path loading works (backs the Presets tab). Only revisit
  removal if the LV2 patch-message load gets solved. Plan in `docs/surge-embed.md`.
- `[~]` **Strong time types (Wave 7 #21) — foundation landed.** `Source/Time.h`
  (`BeatPosition`/`BeatDuration`, `TimePosition`/`TimeDuration`, compile-checked algebra) + a
  `TimeTypes` static-assert test. **Remaining:** incremental adoption — migrate the
  `beatToSamples`/`samplesToBeats` layer + clip/loop/marker positions onto the types, add a
  Sample domain, one subsystem per slice.
- `[~]` **Manual (`docs/`, `mkdocs.yml`).** Material for MkDocs + Diátaxis, two front doors
  (User guide + Control & scripting), the domain model documented **once**; `mkdocs build
  --strict` passes. Keep docs out of implementation-language framing. Deferred: the gRPC/OSC/CL
  reference generators (steps commented in `docs.yml`). See the manual-docs memory note.

So the model layers (tracks, mixer inserts, effects, automation, plugins, sfizz,
composition I/O, recording, presets, session-launch grid, group/submix routing, the
OSC/gRPC surface) all exist. The backlog below is about *depth, musical model, analysis,
product surface, and the session-view/engine tails* — not foundational plumbing.

## The working discipline (the meta-process that works — follow it)

- **Read `AGENTS.md` and `git log --oneline -15` first.** The landmines there are
  non-negotiable; re-learning them costs real time.
- **One slice = one feature = one commit.** Pick the next unchecked backlog item.
  Implement the smallest coherent version that is API-reachable and headless-provable,
  not the whole grand vision at once.
- **The full stack of a slice** (most features touch all of these — miss one and it's
  half-built):
  1. **Model / audio** — a new `Source/<Feature>.cpp` (or extend an existing one);
     realtime data lock-free / under `engineLock`, structural ops via
     `callOnMessageThread` + `pushUndoSnapshot()`.
  2. **Serialisation** — extend `toValueTree`/`loadFromTree` **and** the composition
     mapping in `Source/Composition.cpp` (readable TOML / `.points`), preserving the
     dirty-write round-trip. Add to `tests/smoke.sh`'s round-trip check.
  3. **proto + gRPC** — add messages/RPCs to `proto/gloopy.proto` (proto3;
     `--experimental_allow_proto3_optional`), implement the override in
     `Source/GrpcServer.cpp` (the `main.apiX(...)` → `set_ok`/`set_error` pattern,
     `js()` for `std::string`→`juce::String`). Remember **proto3 omits zero/false** —
     always `.get('x', 0)`, and use OSC for "set to 0 / turn a bool off".
  4. **OSC** — add a `/gloopy/...` lane for anything that's a live knob (`OscControl.h`).
  5. **Clients** — regenerate stubs (`python/gen.sh`) and add thin wrappers to
     `python/gloopy/client.py` (and the CL client where it earns it).
  6. **Register the source** in `CMakeLists.txt` `target_sources` if it's a new `.cpp`.
  7. **Document it** — every new feature/capability lands in the **user-facing manual**
     (`docs/`, Material for MkDocs) *as part of the slice*, not later: what it does and how to
     use it, in the right Diátaxis bucket (User guide for musicians, Control & scripting for
     integrators). Document the domain model **once** in `control-scripting/concepts/model.md`
     and link to it — don't re-explain nouns per language. Keep it out of implementation-language
     framing (describe what it does, not "the C++/JUCE…"). `mkdocs build --strict` must stay
     green. This is not optional: a slice that isn't in the manual is half-built, same as one
     missing its gRPC or desktop surface.
- **Build & prove, synchronously, in one place:**
  ```sh
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target Gloopy
  pkill -x Gloopy; ./build/Gloopy_artefacts/Release/Gloopy &   # ONE instance; -x not -f
  G="grpcurl -plaintext -proto proto/gloopy.proto -import-path proto"
  # ... drive the new RPC, SaveProject/SaveComposition, RenderToFile ...
  python3 - <<'PY'  # assert the WAV: non-silent, peak below 0 dBFS (aim ~ -3 dB)
  ...
  PY
  pkill -x Gloopy
  ```
  For pure-logic slices, add a `GloopyTests` case (`tests/TestMain.cpp`) and run
  `ctest --test-dir build --output-on-failure`. For anything that renders, extend
  `tests/smoke.sh`.
- **Level staging is manual** — there's no auto-master-limiter unless a project adds
  one. Summing tracks clips at 0 dBFS; after any render, assert `max_volume` stays
  below 0 (≈ −3 dB), lowering per-track `volume`/synth `gain` as needed.
- **Commit cadence.** Green build + green tests first. One slice per commit. Source
  headers use `green@moxielogic.com`; commit messages end with
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`. **Commit and push
  frequently** to `origin` (`cave@cave.moxielogic.com:atgreen/gloopy.git`) — one green,
  verified slice = one commit, pushed right away (standing authorization for this repo; see
  AGENTS.md). Commit only a coherent, verified slice; branch first only if asked. Update `docs/ROADMAP.md` (or a new design doc) with the feature's
  goal/approach, **document the new capability in the user-facing manual (`docs/`) — step 7 of
  the full stack, part of the same commit**, and check the item off in this skill's backlog.
- **A slice may span sessions — that's fine.** If you must stop mid-slice: commit the
  green sub-step, and write the exact resumption state (done / half-built / next
  concrete action / open question) into `docs/ROADMAP.md`. Don't fake "done" to
  satisfy a goal or a hook.
- **Consult prior art at real design forks (✦), but adapt, don't copy.** For DAW
  systems (export profiles, location/marker models, bus/send routing, mixer scenes,
  tempo maps, loudness analysis) study Ardour's *shape* and the format conventions,
  then implement the small Gloopy version in Gloopy's idioms. Cross-check any
  third-party mechanism against the AGPL/GPL boundary before adopting. Record which
  prior art a decision followed in the commit or the design doc — by concept, not by
  brand name in shipped text.

## The backlog — the real list we're building, ordered

The through-line: **make the scriptable composition-as-repo surface deep and
complete before making it pretty.** Waves are roughly sequential; within a wave, order
by leverage. Each item is a slice: model+serialise+proto+client → headless proof →
commit. ✦ marks a design fork worth a prior-art check first. Effort: **S** ≈ hours,
**M** ≈ a day or two, **L** ≈ a week+.

### ★ Reprioritized near-term order (updated 2026-07-28) — read this first

**Directive (user, 2026-07-28): do Waves 9 → 10 → 11 FIRST, then go back to the beginning.**
Those three are the cross-cutting **reach & infrastructure** epics — version control, platform,
agent control — that widen who and what can drive Gloopy and refresh the foundation. Front-load
them, *then* return to feature depth (the Phase-B tails below, which pick up the pre-existing
near-term queue).

Already shipped since the 2026-07-26 plan (dropped from the queue; see "Current state"): the
**Surge XT featured-synth front** (DONE/paused — hosted plugin default + bundled presets; removal
blocked by the `.fxp`-into-hosted-plugin blocker, `docs/surge-embed.md`), the **Session view epic**
(slices 1–4), the **grouping / submix-routing epic** (complete), the **mixer channel model**, and
the **UI elevation-over-borders** pass. Also **DONE**: ~~undo/redo~~ (already existed — snapshot
stack, Ctrl+Z/Y, commit c3eedf0; the fine-grained Wave 7 #22 rework stays optional/deferred).

**Phase A — do these now, in this order:**
1. **Wave 9 #30 — Git as project management.** IDE-grade source control (status, commit,
   commit-graph, **branches, tags, checkout any version**, diff, stash, remote/push); shell out to
   the system `git`. **Central** to project management + realizes the "store it in git" half of the
   north star. XL, 12 slices — start at slice 1 (availability + status) and walk them in order.
2. **Wave 10 — build & platform.** **#31 upgrade JUCE** off the 8.0.15 `FetchContent` pin (low-risk;
   do it *before* the port so Windows builds on the latest) → **#32 Windows build** (CI-native
   recommended, else mingw-w64 cross-compile).
3. **Wave 11 #33 — MCP stdio service.** AI agents drive Gloopy over stdio; a thin gRPC adapter —
   the cheapest of the three (pure adapter code over the existing control API).

**Phase B — then go back to the beginning** (resume the pre-existing near-term queue, in order):
4. **Strong time types** (Wave 7 #21) — **foundation landed** (`Source/Time.h`); **NEXT =
   incremental adoption**: migrate the `beatToSamples`/`samplesToBeats` layer + clip/loop/marker
   positions onto the types, add a Sample domain, one subsystem per slice. Before more
   timeline/tempo work.
5. **`AudioBufferPool`** (Wave 7 #24) — not started; kill audio-thread allocations; cheap, and a
   prerequisite for any graph rework.
6. **Finish the Session view epic** — slices **5 (control API: OSC/gRPC launch clip/scene, stop,
   query session state)** and **6 (polish: clip colours, follow-actions, capture, quantize menu)**.
7. **Browser sidebar tail** (Wave 6 #16: Favorites tab + first-class drag-and-drop onto
   tracks/inserts — Templates/Demos/Plugins/Samples/Presets tabs already landed), then the rest of
   Waves 2–5.

The heavy engine items (off-thread graph swap #5, **multicore rendering #6**) stay
*deferred* and gated on a real profiling trigger — do not start them until Gloopy is
measurably single-core-bound on a heavy multi-track session. See Wave 7 for why.

### Wave 1 — Keystone model work (everything downstream rides on these)

1. **Universal parameter model — `ParamModel` / `ParameterRef`.** ✦ **L**
   *Idea #3.* One parameter abstraction: `{ stable id, name, min, max, default,
   scaling (linear|log|dB), unit, current value, thread policy }`. Back UI knobs,
   automation targets, MIDI/OSC mapping, gRPC state, plugin generic editors, and
   save/load with it. Reconcile the existing `TargetRef` (automation/insert params)
   into it so ids are stable across composition files and external clients.
   *Done when:* `ListParameters` returns stable ids for track/insert/effect/plugin
   params; `GetParameter`/`SetParameter` work by id; an automation lane and an OSC map
   both address the same id; ids survive a composition round-trip.
   **✅ DONE** (2026-07-25, focused session across 3 slices) — all four "done when" clauses
   met: `ListParameters` covers track/insert/effect/synth *and* plugin (VST3/LV2) params;
   Get/Set/SetParameterNormalized work by id; automation, the modulation matrix, and MIDI/
   OSC controllers all address the *same* id via the shared `applyParamValue`; and the
   numeric track id is now serialised so `track/<id>/...` ids survive a composition round-
   trip. See the slice log below.
   - `[~]` **Minimal slice landed** (`Source/Parameters.cpp`, commit): flat string-id
     read/write layer — `ListParameters`/`GetParameter`/`SetParameter` RPCs +
     `ParameterInfo{id,name,value,min,max,default_value,unit,scaling}`, delegating to
     the existing thread-safe setters. Covers `track/<id>/{volume,pan,mute,solo}`,
     `track/<id>/synth/<name>`, `insert/<i>/{volume,pan,mute,solo}`,
     `effect/<i>/<slot>/<param>`. Proven via smoke.sh (102 params; cutoff set/get) +
     grpcurl.
   - `[x]` **Automation-by-id + stable track ids landed** (commit, focused session): an
     automation lane can now address a **ParamModel id string** (`AutoLaneSnap.target`),
     written each block through the same lock-held `applyParamValue` that
     controllers/modulation use — so *an automation lane and a controller/LFO map address
     the same id* (the keystone reconciliation). `apiSetAutomationById(target,points)` +
     `apiAddAutomationPointById` (the keyframe primitive) + `SetAutomation.param_id` /
     `AddAutomationPoint` RPCs + Python; serialised on LANE + composition `lanes.toml`
     (`target`). Legacy tuple lanes still work. **Also fixed id stability:** the numeric
     track id is now serialised (`tid` in `.gloopy` TRACK + composition track TOML) and
     preserved on load (`refreshTrackIds` keeps loaded ids, bumps `nextTrackId` past
     them), so `track/<id>/...` param ids survive a composition round-trip. **Desktop UI:**
     the mixer param right-click menu gains "Automate at playhead" / "Clear automation" —
     screenshot-validated. smoke proves a cutoff sweep on `track/<id>/synth/cutoff` moves
     the render, the lane round-trips through SaveComposition/LoadComposition, and the
     RELOADED project still sweeps (stable id keeps the lane live).
   - `[x]` **Stepped (hold) automation landed** (commit): an automation lane can interpolate
     linearly (default) or STEP — hold each point's value until the next (for gated/stepped
     moves). A per-lane `AutoLaneSnap.step` flag read by `interpAuto`; `apiSetAutomationStep`/
     `apiGetAutomationStep` + a SetAutomationStep RPC + Python `set_automation_step`;
     serialised on LANE + composition `lanes.toml`. **Desktop:** a checkable "Stepped
     automation" item on the mixer param right-click menu (reflects + toggles the lane's mode).
     smoke proves a step cutoff lane renders darker than the linear ramp (holds 300 Hz until
     the last point) and the flag survives a composition round-trip (byte-identical re-render);
     screenshot-validated (the checked menu item).
   - `[x]` **Curved (ease-in/ease-out) automation landed** (commit): a per-lane `curve` float
     (-1 ease-out / 0 linear / +1 ease-in) warps `t` within each segment (`interpAuto` uses
     `pow(t, pow(2, 2*curve))`, ignored while stepped). `apiSetAutomationCurve`/
     `apiGetAutomationCurve` + a SetAutomationCurve RPC (`AutoCurveRequest`) + Python
     `set_automation_curve`; serialised on LANE + composition `lanes.toml`. **Desktop:** an
     "Automation curve ▸ Linear / Ease in / Ease out" submenu on the mixer param right-click
     menu (reflects + sets the lane's curve; disabled while stepped). smoke proves an ease-in
     cutoff lane renders darker than the linear ramp (holds low near each segment front),
     differs from it, and the curve survives a composition round-trip (byte-identical
     re-render); screenshot-validated (the expanded submenu with Linear checked). **Not yet:**
     per-segment (as opposed to per-lane) curve shapes, a visual automation-lane editor.
   - `[x]` **Plugin-param ids landed** (commit): hosted VST3/LV2 params now ride the id
     grammar — instrument plugins as `track/<id>/plugin/<index>` and plugin effects as
     `effect/<i>/<slot>/plugin/<index>`, value normalised 0..1. `ListParameters` enumerates
     them (via `AudioProcessor::getParameters()`), `apiGetParameter`/`apiSetParameter` read/
     write by id (`setValueNotifyingHost` from the message thread), and `applyParamValue`
     (audio thread, `setValue`) writes them too — so automation/modulation/controllers can
     all target plugin params by id. Verified against a real hosted plugin (Surge XT: 2855
     params; automation on a plugin param moves the render). smoke is plugin-agnostic +
     conditional (first installed instrument plugin; list + set/get monotonic + automatable
     by id; skipped if none). **Desktop control:** the plugin's own generic/native editor
     (the "Plugin UI" button) edits these params; a *Gloopy-side* generic knob rack to
     attach LFO/MIDI-learn/automation to plugin params from the desktop is the follow-up
     (Wave 6 #19 territory).
     **Follow-up (not keystone-blocking):** the plugin generic-param rack (desktop attach).
   - `[x]` **Param snapshot + scaling helpers landed** (commit, slice 3 — closes the
     keystone): SaveComposition now writes a readable `params.toml` — a non-plugin
     id->value/min/max/scaling/unit manifest so external clients can discover the param
     model from the repo without instantiating plugins (informational; values load from
     each subsystem's own section). Scaling-aware `paramNormalize`/`paramDenormalize`
     (`Source/ParamScale.h`, unit-tested: a log cutoff at knob 0.5 = the geometric mean
     ~632 Hz; linear + dB tapers) back a new `apiSetParameterNormalized(id,pos01)` +
     SetParameterNormalized RPC + Python, so UI knobs / external controllers set a param
     from a 0..1 position honouring its log/dB/linear taper. smoke proves the log-scaled
     normalized set lands at the geometric mean and that `params.toml` lists real ids.
     **Follow-up:** re-taper the actual mixer faders to use dB, and a synth-param knob panel
     (no dedicated synth-param UI knobs exist yet to re-taper).

2. **Timeline locations — markers / ranges / loop / punch / sections.** ✦ **M**
   *Ardour #3.* A project-level `TimelineLocation { kind: marker|range|loop|punch|
   section|export|skip, name, startBeat, endBeat }`. Fold the existing loop/punch
   ranges into it. Store as readable TOML in the composition; surface on the ruler and
   in the API. Named ranges (`intro`, `chorus-2`, `loop-a`) become render/export
   targets.
   *Done when:* `AddLocation`/`ListLocations`/`RemoveLocation` RPCs; locations
   round-trip in the composition; `RenderToFile` can target a named range.
   - `[x]` **Landed** (`Source/Locations.cpp`, commit): `TimelineLocation{name,kind,
     startBeat,endBeat}` model + `AddLocation`(upsert)/`ListLocations`/`RemoveLocation`
     RPCs + `apiResolveRange`; `RenderRequest.range_name` resolves a named range → the
     render window. Serialised in `toValueTree`/`loadFromTree` + composition
     `locations.toml` (survives dir round-trip, dirty-write stable). Python client
     add/list/remove_location + `render(range_name=…)`. smoke.sh asserts render-by-range
     is shorter than full + location survives the composition round-trip.
   - `[x]` **Marker ruler UI landed** (commit): closed the desktop gap — named markers had NO
     desktop control (only `AddLocation` via the API), despite driving split-at-marker. The
     beat-ruler right-click menu gains "Add marker..." (name prompt at the clicked bar →
     `onAddMarker` → `apiAddLocation(name,"marker",beat,beat)`) and "Remove marker (name)" when
     one is near; named markers now DRAW on the ruler as cyan flags + labels (distinct from the
     accent-coloured tempo markers), via the existing `getMarkers` hook. Pure desktop wiring of
     the already-headless-proven Locations API (add/list/remove + composition round-trip already
     smoke-covered), so no proto/Python change. Screenshot-validated (two markers "Verse"/"Drop"
     drawn on the ruler + the "Add marker..." / "Remove marker" menu items).
     **Not yet:** folding the *existing* SetLoop/punch state into this model (today
     they're still separate); export/skip semantics.

### Wave 2 — Turn the control surface into the product

3. **Export profiles / named render targets.** ✦ **M**
   *Ardour #2 + Idea #9.* Named export presets stored in composition text. Targets:
   full mix, named range (Wave 1 #2), selected tracks, stems, current clip, loop
   region. Delivery presets: WAV master, FLAC archive, streaming WAV, loop pack,
   stems. Default outputs to `exports/`, deterministic filenames.
   *Done when:* `DefineExportPreset` + `RunExport(name)` RPCs; a preset renders the
   right files to `exports/` at correct levels; presets round-trip in the composition.
   - `[x]` **Landed** (`Source/Exports.cpp`, commit): `ExportProfile{name,target,
     rangeName,format,trackId,tailSeconds}` + `DefineExportProfile`(upsert)/
     `ListExportProfiles`/`RemoveExportProfile`/`RunExport` RPCs. Targets **mix**
     (whole song), **range** (named location → window), **track** (solo), **stems**
     (one file per instrument track). Reuses `apiRenderToFile` + `apiResolveRange`;
     output to `<project>/exports/` (gitignored) with deterministic
     `<name>.wav` / `stems/<id>-<slug>.wav`; returns the file list. Serialised in
     `toValueTree`/`loadFromTree` + composition `exports.toml`. Python client
     define/list/remove/run_export. smoke.sh: master mix renders a file + profile
     survives composition round-trip. Verified: 6 stems one-per-track, range export
     shorter than mix.
   - `[x]` **FLAC export landed** (commit): `apiRenderToFile` now picks the encoder from
     the output extension (`.flac` → `juce::FlacAudioFormat`, else WAV; both 24-bit),
     and `apiRunExport` chooses `.flac`/`.wav` from the profile's `format` field (which
     already existed). The forced-`.wav` line in the render path now only defaults
     *unknown* extensions. smoke.sh proves a FLAC mix is valid (`fLaC` magic + ffprobe
     codec=flac) and losslessly smaller than the WAV. **Not yet:** streaming/loop-pack/
     current-clip targets, per-format bit depth, MP3/OGG (no encoder linked).

4. **CLI composition utilities.** **M**
   *Ardour #1.* `gloopy` subcommands reusing the GUI load/render code paths, with
   predictable JSON/stdout for scripts and agents: `inspect <proj>`,
   `validate <proj>`, `render <proj> [--preset NAME]`, `export-stems <proj>`,
   `pack <proj> out.zip`. `validate` uses Wave 5 #12 loudness/report data.
   *Done when:* each subcommand runs headless with no GUI, emits stable JSON, and is
   exercised in `tests/smoke.sh` / CI.
   - `[x]` **Landed** (`Source/Cli.cpp` + `Source/Main.cpp` dispatch, commit):
     `gloopy inspect|validate|pack <project>`. New **headless-CLI `MainComponent`
     mode** (`MainComponent(bool headless)`) skips OSC/gRPC/audio so tools run
     alongside a live instance and keep stdout clean (a `CoutSilencer` mutes load
     chatter). `inspect` → JSON {title,bpm,tracks,inserts,locations,exports};
     `validate` → JSON {ok,errors,warnings} (missing SFZ, export→unknown-range,
     zero-length ranges, clip-less tracks) + exit code; `pack` → normalise to a
     composition and zip it (loadable). Works on any input format via
     `openProjectFile`. smoke.sh exercises all three.
   - `[x]` **`validate --loudness` landed** (commit): with the flag, `apiValidateJson`
     prepares the engine (headless-CLI skips the device, so `prepareToPlay` preps the
     generators), renders the whole song offline, `apiAnalyzeFile`s it, and adds a
     `loudness` object + level warnings (true-peak > -1 dBTP → clip risk; > -8 LUFS →
     very hot; near-silent). The render/analyze chatter is wrapped in `CoutSilencer` so
     stdout stays pure JSON. smoke.sh proves it renders non-silent and reports LUFS.
   - `[x]` **`render` + `export-stems` subcommands landed** (commit): `gloopy render
     <project> [out.wav]` (headless offline bounce — prepareToPlay preps the generators,
     apiRenderToFile writes the mix, prints the path) and `gloopy export-stems <project>
     [outdir]` (one WAV per instrument track via a soloed render, emits a JSON stem list).
     Both reuse the headless-CLI MainComponent + CoutSilencer for clean stdout. smoke.sh
     proves both bounce non-silent audio. The `gloopy` CLI surface (inspect/validate/pack
     /scan/analyze/render/export-stems) is now complete.

5. **MIDI import/export + bulk JSON note I/O.** **M**
   *Idea #9/#14 + Ardour #5.* Standard `.mid` file in/out (map to/from the beat-based
   note model), plus `ImportNotesJSON` / `ExportNotesJSON` for fast generative
   workflows. Define a small importer interface (Wave 6 grows it to Hydrogen, SFZ
   folders).
   *Done when:* import a `.mid` → render; export a clip → re-import → note lists match;
   JSON note import builds a clip via the API.
   - `[x]` **MIDI file I/O landed** (`Source/Midi.cpp`, commit): `apiExportMidi` writes
     a Type-1 SMF (tempo track + one track per instrument track, beats→ticks @960 PPQ);
     `apiImportMidi` reads a SMF (`juce::MidiFile`, matched note pairs, ticks→beats,
     tempo meta → transport bpm) into one synth track + clip per MIDI track, reusing
     `apiAddSynthTrack`/`apiAddClip`. RPCs `ExportMidi`/`ImportMidi` (FilePath) +
     Python `export_midi`/`import_midi`. Verified export→import→re-export round-trip
     (bpm + notes survive, renders); smoke.sh asserts SMF magic + reimport→clips→
     non-silent.
   - `[x]` **MIDI export loop-expansion + desktop control landed** (commit): `apiExportMidi`
     now TILES a looped clip's content window across its arrangement length (mirroring
     collectNotes/consolidate — notes clamped so they don't ring past the clip end), so the
     exported .mid matches playback; a one-shot clip still emits once (unchanged). Also closed a
     UI gap: MIDI export was API-only (import had a File-menu item, export didn't) — added a
     "Export MIDI File..." item to the File menu (save FileChooser → apiExportMidi, defaults the
     .mid extension). smoke proves a 2-beat-content/4-beat looped clip exports its tiled notes
     (0/1/2/3, not just 0/1) via export→NewProject→import→GetClipNotes; screenshot-validated
     (the File menu item). **Not yet:** multi-channel/CC import, per-clip (not per-track)
     granularity.
   - `[x]` **Notes JSON + clipboard copy/paste landed** (commit): `ExportNotesJSON` emits a
     clip's notes as a compact JSON array `[{pitch,start,length,velocity},...]`;
     `ImportNotesJSON` builds a new clip on a track at a beat from that JSON (clip length =
     furthest note end). The parse/format is a pure header (`Source/NotesJson.h`, tolerant of
     missing keys / a `{notes:[...]}` wrapper / clamps, unit-tested `NotesJsonTests`) shared by
     the RPCs + Python `export/import_notes_json`. **Desktop:** clip right-click "Copy notes
     (JSON)" -> system clipboard; right-click empty track space -> "Paste notes here" builds a
     clip at that beat from the clipboard. smoke proves an export->import round-trip preserves
     pitch/start/length; the GUI copy->paste was screenshot- AND functionally-validated (paste
     created a clip with the copied notes). Gotcha logged: proto3 omits index 0, so a
     successful import (new clip idx 0) reads back as absent, not -1.

### Wave 3 — Editing + routing depth (still API-first)

6. **Clip / region operations.** **M**
   *Ardour #12.* Split at playhead / at marker, clip gain, normalize, reverse audio,
   per-clip fades (audio), duplicate-to-grid, crop-to-range, consolidate,
   bounce-in-place. All through the API and expressible in the composition.
   *Done when:* each op has an RPC; a split/gain/reverse sequence round-trips and
   renders correctly.
   - `[x]` **Split/duplicate/reverse + note read-back landed** (`Source/ClipOps.cpp`,
     commit): `apiSplitClip` (absolute beat → two clips, notes distributed, straddlers
     truncated), `apiDuplicateClip` (at beat, or butt-up-after), `apiReverseClip`
     (mirror notes in the content window; reverse the audio buffer for audio clips),
     `apiGetClipNotes` (read-back — also covers #5's deferred note-export). RPCs
     SplitClip/DuplicateClip/ReverseClip/GetClipNotes + Python client. Clips already
     serialise, so no new persistence. Verified precisely via GetClipNotes (split
     8→4+4 with correct offsets; reverse 60→beat3/63→beat0; duplicate→3 clips) +
     smoke.sh.
   - `[x]` **Repeat clip (tile) landed** (commit): `apiRepeatClip(trackId,index,copies)` appends
     N back-to-back duplicates after a clip (each one clip-length further along), turning a
     1-bar loop into an N-bar run — the "duplicate-to-grid" variant from #6. Copies share
     audio buffers (read-only) and duplicate MIDI notes. RepeatClip RPC (→SliceResult) +
     Python. **Desktop UI:** a "Repeat ▸ x2/x4/x8/x16" submenu on the clip menu (adds N-1
     copies for an N× total) — screenshot-validated. smoke proves a 2-beat clip repeated x4
     saves as 4 clips at beats 0/2/4/6.
   - `[x]` **Loop-to-clip landed** (commit): `apiSetLoopToClip(trackId,index)` sets the
     transport loop region to a clip's [start,end) and enables looping (audition a clip on
     repeat). `TransportState`/`GetTransport` gained `loop_enabled`/`loop_start`/`loop_end`
     so clients (and the smoke test) can read the loop back. SetLoopToClip RPC (ClipRef) +
     Python; `transport()` now returns the loop fields. **Desktop UI:** "Loop this clip" on
     the arrange-view clip menu — screenshot-validated (Loop button lights + the ruler draws
     the region over the clip). smoke proves SetLoopToClip on a clip at [5,8) sets the loop
     region there (verified via the new GetTransport loop fields).
   - `[x]` **Audio-clip split fixed** (commit): `apiSplitClip` on an audio clip previously
     copied the *shared* full buffer into both halves, so the right half REPLAYED from the
     buffer start at the cut instead of continuing. Now each half's buffer is trimmed to its
     own span (source-sample offset via the tempo-aware beats→seconds→samples map, same as
     crop); peaks rebuilt, inner fades cleared, `audioFile`/`takeId` cleared so the halves
     embed. Fixes the existing "Split at playhead" clip-menu control for audio. smoke proves
     the split is audibly TRANSPARENT (whole-render mean-abs diff 0.000000 before vs after a
     split of a two-distinct-halves clip — a replay bug would corrupt the second half).
   - `[x]` **Slice at transients landed** (commit, needed the audio-split fix): a pure
     energy-flux onset detector (`Source/Onsets.h`, `detectOnsets` — short-frame log-energy
     rise, peak-picked above mean+sensitivity·std with a 50 ms min gap, no FFT, unit-tested)
     drives `apiSliceClipAtTransients(trackId,index,sensitivity)` — it detects onsets in an
     audio clip's buffer, maps each onset source-sample → absolute beat (tempo-aware), and
     splits the clip left-to-right at each (cutting the fresh right piece each time). RPC
     (→SliceResult) + Python. **Desktop UI:** "Slice at transients" on the audio-clip menu —
     screenshot-validated. smoke proves 4 staccato hits slice into ~4 clips; the detector
     itself is unit-tested (hits at 0/4000/8000/12000 → 3 interior onsets; silence → none).
   - `[x]` **Per-clip mute landed** (commit): `apiSetClipMuted(trackId,index,muted)` disables a
     clip in the arrangement without deleting it. Fixed a real gap: the MIDI `collectClip`
     ignored `clip.muted` (only the audio renderer honoured it), so muting a MIDI clip was a
     no-op — now both skip muted clips. `clip.muted` already serialises. SetClipMuted RPC +
     Python. **Desktop UI:** "Mute clip" (checkable) on the arrange-view clip menu, for
     non-take clips (takes use Use/Promote) — screenshot-validated. smoke proves a muted MIDI
     clip renders silent (-13 -> -144 dBFS) and unmuting restores it.
   - `[x]` **Clip gain + normalize landed** (commit): `apiSetClipGain` (audio clip gain
     in dB) and `apiNormalizeClip` (scan the clip buffer's peak, set gain so it hits a
     target dBFS; returns the applied gain). Audio clips only (MIDI dynamics = velocity);
     both under engineLock via the reverse-clip pattern. SetClipGain/NormalizeClip RPCs +
     Python. **Desktop UI:** `Normalize` + `Gain...` on the arrange-view clip right-click
     menu (audio clips only; Gain prompts for dB) — screenshot-validated on Xvfb.
     smoke.sh proves it via deltas (insert loss cancels): normalize -6 vs -18 renders
     12 dB apart; SetClipGain -6 dB drops the peak 6 dB.
   - `[x]` **Per-clip fades landed** (commit): `Clip.fadeInBeats`/`fadeOutBeats` + linear
     fade edges applied in `renderAudioClip` (tempo-aware lengths, silent at the very
     start/end). `apiSetClipFades` + SetClipFades RPC + Python; serialised in the
     ValueTree (`fadein`/`fadeout`) and the composition (`fade_in`/`fade_out`), so they
     round-trip. **Desktop UI:** `Fades...` on the audio-clip menu (in/out beats prompt)
     — screenshot-validated. smoke.sh proves a 2-beat fade-in drops the first 0.25 s by
     ~16 dB vs no fade.
   - `[x]` **Crop-to-range landed** (commit): `apiCropClip(trackId,index,start,end)` trims a
     clip to the absolute beat intersection. MIDI: the clip moves/shrinks and notes
     overlapping the window are kept (onset/length clamped, re-based). **Audio: the sample
     buffer is trimmed to the [start,end) window** (beat span → wall-clock seconds via the
     tempo map → source samples; peaks rebuilt, fades reset, `audioFile`/`takeId` cleared so
     the cut buffer embeds). CropClip RPC + Python. **Desktop UI:** "Crop to loop region"
     on the arrange-view clip menu (MIDI *or* audio clips, enabled when a loop is set) →
     crops to the transport loop — screenshot-validated on both. smoke proves MIDI
     [0,1,2,3]→crop[1,3) keeps 62@0/64@1 + rejects empty ranges, and that an audio clip
     cropped [2,4) saves as start=2/len=2 with the buffer cut to exactly the 2-beat window.
   - `[x]` **Consolidate landed** (commit): `apiConsolidateClip(trackId,index)` flattens a
     looped MIDI clip — it writes out every repetition's notes as explicit notes at their
     absolute positions (mirroring the render's beat-space tiling in `collectClip`;
     onsets past the clip end dropped, tails truncated to the clip) and un-loops the clip
     (`contentLen`=`length`, `looped`=false), so each repetition can then diverge. A
     one-shot clip is already flat (no-op success); MIDI only. ConsolidateClip RPC (ClipRef)
     + Python. **Desktop UI:** "Consolidate loops" on the arrange-view clip menu, enabled
     only for MIDI clips that actually tile (looped + content<length) — screenshot-validated.
     smoke proves a 2-beat content tiled over 4 beats → notes at 0/1/2/3 and the clip saves
     un-looped (content=len=4).
   - `[x]` **Bounce-in-place landed** (commit): `apiBounceClip(trackId,index)` freezes a
     clip to audio — it renders just that track over the clip's [start,end) region offline
     and soloed (reusing `apiRenderToFile`), then re-imports the WAV as an embedded audio
     clip on a fresh "<name> (bounce)" audio track. **Non-destructive** (source untouched);
     works for MIDI (prints the instrument) and audio (prints the insert chain). BounceClip
     RPC (ClipRef→TrackId) + Python. **Desktop UI:** "Bounce to audio" on the clip menu —
     screenshot-validated. smoke proves the bounce makes a 1-clip audio track that renders
     non-silent while the source MIDI clip stays intact. **Known limitation:** the bounce
     is *not* level-matched — the soloed render already bakes in the track's insert/pan/
     master, and the new audio track re-applies its own insert/pan/master, so the frozen
     level differs (verified identical to a manual audio round-trip, so it's a mixer-hop
     cost, not a bounce bug). A level-exact freeze needs a pre-master / pre-pan capture
     path — follow-up.
   - `[x]` **Split-at-named-marker landed** (commit): `apiSplitClipAtMarker(trackId,index,
     marker)` resolves a named timeline location's beat from `locations` and delegates to
     `apiSplitClip` (no-op if the marker falls outside the clip). SplitClipAtMarker RPC
     (→ClipId) + Python. **Desktop UI:** a "Split at marker ▸ <names>" submenu on the clip
     menu, listing only markers that fall strictly inside the clicked clip (via a `getMarkers`
     hook) — screenshot-validated. smoke proves a marker at beat 2 splits a [0,4) clip into
     [0,2)+[2,4) with the right clip's notes rebased to 0/1.
     **Not yet:** level-matched freeze (pre-master capture);
     audio-clip consolidate (audio clips are one-shot).
   - `[x]` **Fade curve shapes landed** (commit): an audio clip's fade edges gain a `fadeShape`
     (0 linear / 1 equal-power / 2 exponential) — a pure `fadeShapeGain(shape, t)` helper in
     `Source/FadeShape.h` warps the linear fade position, shared by both edges. Equal-power
     (sin·π/2, the constant-power crossfade law from Ardour/Reaper) is louder than linear;
     exponential (t²) is a gentle slow start. `apiSetClipFadeShape` + SetClipFadeShape RPC
     (`ClipFadeShapeRequest`) + Python `set_clip_fade_shape`; serialised on the CLIP ValueTree
     (`fadeshape`, omitted when 0) + composition `fade_shape`. **Desktop:** a "Fade shape ▸
     Linear / Equal power / Exponential" submenu on the audio-clip right-click menu. `FadeShape`
     unit test proves the curve ordering/endpoints/clamp; smoke proves fade-region RMS orders
     equal-power > linear > exponential and the shape survives a project round-trip (reproducible
     re-render); screenshot-validated (the expanded submenu).

7. **Buses & sends.** ✦ **L**
   *Ardour #8.* Explicit bus tracks; tracks route to a bus before master; per-send
   levels; signal flow generator → track inserts → sends → bus inserts → master. Send
   presets (vocal reverb, drum parallel comp, delay throw). Keep it minimal — no
   general patchbay.
   *Done when:* a reverb bus with two sends renders the expected wet/dry mix; routing
   round-trips in the composition.
   - `[x]` **Landed** (`Source/Buses.cpp` + audio mix loop, commit): `apiAddBus`
     (append a bus mixer track — `MixerTrack.isBus`, higher index so its buffer
     accumulates sends before it's processed) and `apiSetSend` (upsert an aux send;
     `MixerTrack.Send{bus,level}`, level≤0 removes). The mix loop taps each insert's
     **post-effects** signal into its target buses (additive, independent of
     mute/solo); the bus processes its own effects and sums to master like any insert.
     Serialised in `toValueTree`/`loadFromTree` (MTRACK `bus` prop + SEND children,
     with the FX-load loop now guarding `hasType("FX")`) + composition `inserts.toml`
     (`bus` flag + `sends = ["busIdx,level", …]`). RPCs AddBus/SetSend, `Send`/`is_bus`
     on MixerInsert; Python `add_bus`/`set_send`. Verified: reverb bus lifts tail RMS
     0.040→0.189 (4.7×), routing round-trips. smoke.sh asserts the routing.
   - `[x]` **RemoveBus landed** (commit): `apiRemoveBus` removes a bus mixer track (buses
     only — not master/regular inserts) and **re-indexes sends** across every insert (drops
     sends targeting it, decrements sends targeting higher indices, since mixerTracks
     indices are the send address space). RemoveBus RPC (reuses TrackId as the index) +
     Python `remove_bus`. smoke.sh proves a send follows its bus down when a lower bus is
     removed, and a non-bus (master) is rejected. Sends-in-scenes done last tick.
   - `[x]` **Bus/send desktop UI landed** (commit): the whole bus/send subsystem was API-only;
     now the mixer strip's name right-click menu (showGroupMenu) gains a "New bus..." item (name
     prompt → apiAddBus) and, per existing bus, a "Send to <bus> ▸ Off/25/50/75/100%" submenu that
     reflects + sets the send level (apiSetSend), sourced from `apiListInserts` (isBus + the
     insert's `sends`). New MixerView hooks onListBuses/onInsertSends/onSetSend/onAddBus wired to
     the existing Buses.cpp API — no proto/API change (the routing is already headless-proven by
     the reverb-bus smoke). Screenshot-validated (two buses created from the desktop, the strip
     menu's New bus + "Send to Reverb Bus ▸ ✓Off/25/50/75/100%" submenu).
   - `[x]` **Pre/post-fader send choice landed** (commit): `MixerTrack.Send.postFader` — a PRE-fader
     send taps the post-fx signal at its own level regardless of the fader/mute (a classic aux); a
     POST-fader send follows the fader gain (incl. the group VCA) and is silenced when the channel
     is muted/soloed out. The mix loop now computes the fader gain + audibility BEFORE the send tap
     so post-fader can scale by it. `post_fader` on the SetSend RPC + Python `set_send(post_fader=)`
     + apiSetSend; serialised on the SEND ValueTree (`post`, omitted when false) + composition
     (`bus,level,post`). **Desktop:** the "Send to <bus>" submenu gains a Pre-fader/Post-fader radio
     (enabled once a send exists, checkmarked). Scene recall only updates send *levels* by bus so it
     leaves post untouched (scenes capture level, not the tap point). smoke: a MUTED source's
     pre-fader send still routes to the bus (rms 1.42M) while its post-fader send is silenced
     (919k), and the post flag survives a project round-trip; screenshot-validated (the ✓Pre-fader /
     Post-fader items under the level presets).
     **Not yet:** send presets; a dedicated send-level fader per strip (menu presets for now).

   **✅ Wave 3 complete** (#6 clip ops, #7 buses/sends, #8 mixer scenes).

8. **Mixer scenes + control groups (VCA-lite).** **M**
   *Ardour #9/#10.* Named mixer snapshots (rough mix, vocal up, print mix) capturing
   faders/pans/mutes/solos/send levels/bypass, stored as small composition files;
   automation stays separate. Control groups = group fader/mute/solo/color as control
   *scaling*, not extra audio routing.
   *Done when:* snapshot → change faders → recall restores them (assert via
   `GetState`); a group fader scales its members; both round-trip.
   - `[x]` **Mixer scenes landed** (`Source/MixerScenes.cpp`, commit):
     `DefineMixerScene`(snapshot, upsert)/`ListMixerScenes`/`RecallMixerScene`/
     `RemoveMixerScene`. Captures per-insert vol/pan/mute/solo + each effect's bypass;
     recall tolerates added/removed inserts/effects. Serialised in
     `toValueTree`/`loadFromTree` (SCENES/SCENE/INSERT) + composition `scenes.toml`
     (inserts as compact `"vol,pan,mute,solo,bypassbits"` strings via the flat TOML
     writer). Python client + smoke.sh (snapshot→mangle→recall restores exactly).
     Verified vol/pan/mute + effect-bypass restore, composition round-trip.
   - `[x]` **Aux-send levels now captured** (commit): scenes also snapshot each insert's
     `sends` (bus,level); recall restores the level of every still-existing send (matched
     by bus, tolerating added/removed sends like inserts/effects). Encoded as a
     `"bus:level|..."` string in the SCENE/INSERT ValueTree and appended as a 6th field to
     the composition's compact insert string (back-compatible: old 5-field rows parse with
     no sends). smoke.sh: set a send, snapshot, change it, recall -> level restored.
   - `[x]` **Control groups (VCA-lite) landed** (`Source/ControlGroups.cpp`, commit): a named
     group whose fader SCALES its member inserts' volumes (control scaling, not audio
     routing — the mix multiplies each insert's `v` by its group's gain and silences it if
     the group is muted). Membership is a `group` name carried ON the insert
     (`MixerTrack.group`), so it survives insert re-indexing. `apiDefineControlGroup`/
     `SetControlGroupGain`/`SetControlGroupMute`/`AssignInsertToGroup`(""=clear, defines if
     new)/`RemoveControlGroup`/`ListControlGroups` + RPCs + Python. Serialised in the
     ValueTree (`GROUPS/GROUP` + a `group` attr per `MTRACK`) AND the composition
     (`groups.toml` + a `group` field per insert in `mixer/inserts.toml`). **Desktop UI:** right-click a
     mixer strip name -> control-group menu (New group.../Assign to/Group gain 0-100%/Mute/
     Delete) — screenshot-validated. smoke proves gain 0.5 drops a member's soloed render
     exactly 6 dB, mute silences it, and the group + membership survive both a `.gloopy`
     round-trip and a SaveComposition/LoadComposition round-trip.
   - `[x]` **Group solo (VCA solo) landed** (commit): `ControlGroup.solo` — soloing a group
     makes ONLY its members audible in the full mix (folded into the mix loop's `audible`
     test alongside per-track solo: while anything is soloed, an insert plays iff it is
     directly soloed OR belongs to a soloed group). `apiSetControlGroupSolo` + SetControlGroupSolo
     RPC + Python `set_control_group_solo`; `solo` added to `ListControlGroups`. Serialised on
     the `GROUPS/GROUP` ValueTree and `groups.toml` (omitted when false). **Desktop:** a "Solo
     group" toggle on the mixer strip's control-group menu. smoke proves a soloed group's
     full-mix render is byte-identical to a T1-only render (T2 dropped) and that the solo flag
     survives a project round-trip; screenshot-validated (the "Solo group" menu item).
     **Not yet:** a dedicated group fader strip in the mixer (menu-driven gain for now).

### Wave 4 — Musical model & modulation

9. **Modulation matrix — LFO/envelope → any `ParamModel`.** ✦ **M** (needs Wave 1 #1)
   *Idea #12.* Modulation sources (LFO, envelope) route to any parameter id with
   tempo-sync, phase, shape, depth, smoothing, bipolar/unipolar. Persist routes as
   readable composition files; expose over gRPC.
   *Done when:* an LFO modulating a filter-cutoff param audibly moves it in a render;
   routes round-trip.
   - `[x]` **LFO landed** (`Source/Modulation.cpp`, commit): `Mod{target,rate,depth,
     center,shape}` LFO on any ParamModel id — `value = center + depth*osc(rate·t)`,
     shapes sine/tri/saw/square. `evaluateModulation(timeSeconds)` runs on the audio
     thread **under engineLock, mirroring `evaluateAutomation`'s shape**, driven off
     the playhead so renders are deterministic; a new lock-held `applyParamValue`
     writes any ParamModel id directly (audio-thread-safe, no message thread).
     `apiSetModulation`(upsert)/`apiRemoveModulation`/`apiListModulations`. Serialised
     (MODS/MOD + composition `mods.toml`). RPCs + Python. Verified: cutoff LFO changes
     the render (mean abs diff 0.033), shapes differ, round-trips.
   - `[x]` **Tempo-sync landed** (commit): `Mod.syncBeats` (>0 = cycle length in beats).
     The pure phase/osc math moved to `Source/Lfo.h` (`lfoPhaseCycles`/`lfoOsc`) so it's
     unit-testable without the engine — a synced LFO takes its phase from the beat
     position (`evaluateModulation` now gets `tc.sampleToBeat(playhead)`), so its period
     tracks the tempo map; a free LFO still uses rate·seconds. `sync_beats` on the
     SetModulation RPC + ListModulations + Python; serialised in MODS + `mods.toml`; the
     mixer "Add LFO" prompt gains a Sync field. `GloopyTests::Lfo` proves the sync-vs-free
     phase (and caught a wrong triangle expectation); smoke proves a synced LFO modulates
     the render and `sync_beats` round-trips.
   - `[x]` **Phase offset + unipolar landed** (commit): `Mod.phase` (0..1 cycle offset) and
     `Mod.unipolar` (value ranges center..center+depth instead of center +/- depth). The
     folding math is a new `lfoUnit(shape, phase, offset, unipolar)` in `Source/Lfo.h`
     (unit-tested in `GloopyTests::Lfo`). `phase`/`unipolar` on SetModulation + ListModulations
     + Python; serialised in MODS + `mods.toml` + composition. **Desktop UI:** the mixer "Add
     LFO" prompt gains a Phase field and a Bipolar/Unipolar Range selector (screenshot-
     validated). smoke proves a half-cycle phase shift changes a synced-saw render and that
     phase 0.25 + unipolar round-trip.
   - `[x]` **Slew / smoothing landed** (commit): `Mod.slewMs` — a one-pole slew (ms time
     constant) applied per block to the modulation value, softening abrupt shape edges
     (square/saw) and zipper noise. Transient smooth state (`smoothState`/`smoothInit`, not
     serialised) is seeded on the first block and reset at the start of every offline render
     (`resetModulationSmoothing`) so bounces are deterministic. `slew_ms` on SetModulation +
     ListModulations + Python; serialised in MODS + `mods.toml` + composition. **Desktop UI:**
     the "Add LFO" prompt gains a "Smooth (ms)" field (screenshot-validated). smoke proves a
     40 ms slew softens a fast square LFO (render differs) and `slew_ms` round-trips.
   - `[x]` **Random / sample-and-hold LFO shape landed** (commit): a 5th LFO shape (shape 4)
     that holds a stepped random value for each cycle. The random value is a fixed integer
     bit-mix of the cycle index (`lfoHash` in `Source/Lfo.h`) — **no RNG state**, so offline
     bounces stay byte-identical (principle 4). Pairs naturally with the existing slew for
     "wandering" (smooth-random) modulation. Just widened the shape clamp (0..4) and added
     "Random (S&H)" to the Add-LFO shape combo — no proto/serialisation change (shape is
     already an int). `GloopyTests::Lfo` proves S&H is held within a cycle, steps + is
     deterministic across cycles, and stays in [-1,1); smoke proves a random-cutoff render
     differs from static AND is identical across two renders (reproducible), shape=4 round-
     trips; screenshot-validated (the expanded shape combo lists Random (S&H)).
   - `[x]` **Multiple modulation sources per target (they sum) landed** (commit): more than one
     LFO can target the SAME ParamModel id and they now SUM instead of the last one winning.
     `evaluateModulation` groups by target (allocation-free O(n^2) scan, no audio-thread heap)
     and writes `center(first source) + sum(depth_i*osc_i)` once per target. `apiSetModulation`
     is now a canonical single-set (clears any sources already on the target); a new
     `apiAddModulation`/`AddModulation` RPC APPENDS an additional source; `RemoveModulation`
     clears them all. The single-source render is byte-identical (slewing the delta is
     equivalent to slewing center+delta since center is constant). Serialisation already looped
     the mod vector, so multiples round-trip with no schema change. UI: the mixer "Add LFO..."
     now STACKS (each invocation appends). Python `add_modulation`. smoke proves a 2nd LFO
     stacks + changes the render, 2 sources on the target survive a composition round-trip, and
     Remove clears all; desktop-validated by driving "Add LFO..." twice via the GUI so
     ListModulations reports 2 sources on the param.
     **Not yet:** envelope sources, per-source edit/remove UI (needs a mod-matrix view, #19),
     plugin-param targets.

10. **Tempo & time-signature map.** ✦ **M**
    *Ardour #13.* Tempo / time-signature changes on the timeline, stored as tempo
    markers in the composition. Note/clip data stays beat-based, rendering
    sample-based; add API helpers for bars/beats ↔ beats/seconds.
    *Done when:* a tempo change mid-song changes the rendered duration as computed;
    tempo markers round-trip.
    - `[~]` **Model + conversion helpers landed** (`Source/Tempo.cpp`, commit): tempo
      map = sorted `{beat,bpm}` markers (empty ⇒ constant `transport.bpm`, unchanged);
      exact piecewise `apiBeatsToSeconds`/`apiSecondsToBeats`/`tempoAtBeat`;
      Add/List/Remove marker (upsert by beat). Serialised (TEMPOMAP + composition
      `tempo.toml`). RPCs + Python. Verified: 8 beats across 120→240 = 3.0s (not 4.0),
      round-trips.
    - `[x]` **Conversion foundation landed** (commit): `beatToSamples`/`samplesToBeats`
      (tempo-aware, byte-identical to `beat*spb` / `samples/spb` when the map is empty;
      engineLock is recursive so they're callable from `renderBlock`).
    - `[x]` **Scheduler rewrite landed** (commit): renderBlock snapshots the markers
      once per block into an **allocation-free `TempoConv`** (NoteScheduler.h) — the
      empty-map case is the constant `llround(beat*spb)` path (byte-identical), a
      non-empty map integrates the tempo piecewise over a fixed-size marker snapshot, so
      the audio thread never locks or allocates to convert (principle 4). `collectClip`
      now tiles repetitions in **beat space** (`beatToSample(startBeat + k*repBeats)`),
      and `collectNotes` takes an absolute `repStartBeat` + the `TempoConv` and places
      each note at its absolute song-sample position, filtered to the block window. Also
      routed through the map: `loopLen`, the loop window, the seek target, the automation
      beat, audio-clip anchors, `apiRenderToFile` range endpoints, and MIDI-record beats;
      `loadFromTree` sorts the map (the snapshot needs beat-ascending). Proven: a
      `GloopyTests` case asserting the exact integration of a mid-song 120→240 change
      (sample positions 50/100/125/150) + `collectNotes` honouring it, and a `smoke.sh`
      assertion that a mid-song speed-up shortens a fixed 0..4-beat render while staying
      above half (pre-marker beats preserved). Empty-map smoke suite stays green.
    - `[x]` **Time signature landed** (commit): a project time signature on the transport
      (`{num,denom}`, default 4/4) with `beatsPerBar()` in quarter-note beats (4/4→4,
      3/4→3, 6/8→3, 7/8→3.5). SetTimeSignature/GetTimeSignature + a bars↔beats conversion
      API (`BeatsToBarBeat`/`BarBeatToBeats`, 1-based "bar.beat") — the roadmap's "bars/
      beats↔beats helpers." Serialised (root `tsnum`/`tsden` + composition manifest).
      The `beatsPerBar` constant in ArrangeView/PlaylistView became a member refreshed
      from the transport (call sites unchanged), and the "1.1.00" position readout is now
      time-sig-aware. **Desktop UI:** "Time signature..." on the beat-ruler right-click
      menu (num/denom prompt) — screenshot-validated (a 12-beat clip spans 4 bars at 3/4,
      not 3 at 4/4). smoke proves the conversions (4/4 beat6=bar2.3 vs 3/4 beat6=bar3.1,
      inverse round-trips) + save/reload. **Not yet:** time-signature *map* (per-bar
      changes), and the PianoRoll/StepEditor internal `%4` bar markers.

11. **Scales & microtuning.** **M**
    *Idea #11.* Project-level scale definitions; per-track tuning mode; piano-roll
    scale highlighting + optional snap-to-scale; API helpers for generative clients;
    Scala/keymap import later.
    *Done when:* set a project scale via API, snap a note list to it, render; scale
    defs round-trip.
    - `[x]` **Scales + snap landed** (`Source/Scales.cpp`, commit): project scale
      `{root, name, intervals}` set by explicit intervals or a built-in name (major,
      minor, modes, pentatonics, blues, whole-tone, chromatic); `apiSetScale`/
      `apiGetScale`; `apiSnapClipToScale` snaps each note to the nearest scale degree
      (`snapPitchToScale`, ties round up). Pure metadata + pitch math — no audio path.
      Serialised on the root ValueTree + composition manifest (`scale_root`/
      `scale_name`/`scale_intervals`). RPCs SetScale/GetScale/SnapClipToScale; Python
      set/get_scale + snap_clip_to_scale. Verified chromatic→C-major snap (correct
      tie-ups) + round-trip.
    - `[x]` **Per-track detune (microtuning) landed** (commit): a whole-voice `detune`
      (cents, ±2 octaves) on the built-in synth — `baseFreq *= 2^(cents/1200)` at note-on.
      Rides the universal param model as `track/<id>/synth/detune` (SetParameter/GetParameter
      /modulation/automation all free) and serialises with the other synth params.
      smoke proves the cents→frequency mapping exactly: +1200 cents doubles a sine's
      zero-crossing rate (ratio 1.999) and the value round-trips.
    - `[x]` **Per-pitch-class microtuning + Scala import landed** (commit): a project-level
      12-entry cents-offset-from-ET table (`projectTuning`, all 0 = 12-TET) applied at the
      built-in synth's note-on alongside `detune` (`baseFreq *= 2^((detune + tuning[note%12])
      /1200)`). Stored in `SynthParams.tuning` (12 atomics), broadcast to every synth by
      `applyTuningToSynths` (new tracks inherit it; load re-applies). `apiSetTuning`/
      `apiGetTuning` + `apiImportScl` (a Scala `.scl` parser in `Source/Scales.cpp`: skips
      description + note-count, reads cents `x.y` / ratios `a/b`/`n`, maps degree i to pitch
      class i's offset = cents − i·100). RPCs SetTuning/GetTuning/ImportScl + Python. Serialised
      on the root ValueTree + composition manifest (`tuning_cents`, omitted for 12-TET).
      **Desktop:** File → "Load Tuning (.scl)..." (chooser) + "Reset Tuning (Equal)". smoke:
      +1200c on C doubles a sine (ZCR 2.005), survives a composition round-trip, and a test
      `.scl` (degree 1 = 150c) yields class-1 offset +50; screenshot-validated. Gotcha logged:
      the `.scl` state machine needs explicit have-description/have-count flags — a `< 0`
      sentinel for "description seen" swallowed the note-count line. **Not yet:** SFZ/plugin
      microtuning, per-note (not per-class) tables, `.kbm` keyboard maps, piano-roll UI.

### Wave 5 — Analysis, plugins, diagnostics (offline / headless-friendly)

12. **Offline loudness + render reports.** **S/M**
    *Ardour #14.* After a render, report peak, true-peak, RMS, and LUFS (EBU R128);
    feed `gloopy validate` (Wave 2 #4) and a render report. Transient/onset detection
    later (for slicing).
    *Done when:* `RenderToFile` optionally returns/writes a report; the numbers match a
    known test signal within tolerance.
    - `[x]` **Landed** (`Source/Loudness.cpp`, commit): `apiAnalyzeFile` computes
      peak (dBFS), true-peak (dBTP, 4x windowed-sinc oversampled), RMS (dBFS), and
      integrated LUFS (ITU-R BS.1770 — hand-rolled RBJ K-weighting biquads correct at
      any rate + gated 400ms/100ms blocks, abs −70 / rel −10). No juce_dsp, no
      audio-thread. `AnalyzeFile` RPC + `gloopy analyze <wav>` CLI (JSON) + Python
      `analyze_file`. Verified against a −20 dBFS 1 kHz tone (peak −20.0, RMS −23.01,
      LUFS −23.26 vs ~−23.6 ref) and the smoke render.
    - `[x]` **Wired into RenderToFile's return** (commit): `RenderRequest.report` (bool)
      makes `RenderToFile` return a `RenderResult { ok, error, LoudnessReport report }`
      — one call for "render + measure" in CI/scripts. `RenderResult` is wire-compatible
      with `Ack` on fields 1/2, so existing clients are unaffected; Python `render(...,
      report=True)` returns the loudness dict. smoke.sh asserts the inline report equals
      a standalone `AnalyzeFile` and is non-silent.
    - `[x]` **Momentary + short-term LUFS + LRA landed** (commit): extends `apiAnalyzeFile`
      with max momentary (400 ms window — reuses the integrated-LUFS block loop), max
      short-term (3 s / 1 s-hop windows), and LRA (EBU Tech 3342: abs gate −70, relative
      gate −20 LU, P95−P10 of gated short-term). Added to `LoudnessReport` + the proto +
      the `AnalyzeFile`/`RenderToFile`-report paths + `gloopy analyze` JSON + Python.
      smoke proves on a 6 s steady sine that momentary ≥ integrated, short-term ≈
      integrated, and LRA is bounded. **Not yet:** `gloopy validate` render+analyze wiring
      of these. **Transient/onset detection landed** as `Source/Onsets.h` (`detectOnsets`),
      consumed by the audio-clip "Slice at transients" op (Wave 3 #6).

13. **Plugin scan cache + CLI scan.** **S/M**
    *Ardour #15.* Persist scan results (id, name, format, path, vendor, category,
    parameter summary, scan status); record failed scans without blocking startup;
    keep plugin identity stable in composition files even if the path moves; add a CLI
    `gloopy scan`.
    *Done when:* a scan writes a cache, a moved-path plugin still resolves on load, and
    `gloopy scan` runs headless.
    - `[x]` **Landed** (commit): enriched `PluginInfo`/`PluginSnap` with vendor,
      category, version, and input/output channel counts (from the cached
      `PluginDescription`); `ListPlugins`/`ScanPlugins` return them; a headless
      `gloopy scan [--force]` CLI prints the cached list as JSON (valid empty array
      with zero plugins). The scan already persists to `~/.config/Gloopy/plugins.xml`
      and load re-resolves by identifier (stable across path moves). Verified: 68
      cached plugins with vendor/category/channels; smoke asserts valid JSON.
      **Not yet:** per-plugin parameter summary (needs instantiation), failed-scan
      status records, blocklist.

14. **Real-time safety diagnostics.** **S/M**
    *Ardour #16.* A small diagnostics surface: audio-callback time, DSP load,
    xruns/dropouts, plugin latency, render speed, device settings — over the API and in
    CI smoke logs. Debug counters/asserts for allocations/locks on the audio path;
    mark xrun events on the timeline.
    *Done when:* `GetDiagnostics` returns live counters; the smoke log includes them.
    - `[x]` **Landed** (`Source/Diagnostics.cpp`, commit): `GetDiagnostics` RPC →
      device sample-rate/block-size/inputs/outputs, live audio-callback time (last +
      max µs) and DSP load, dropped-block count (engine-lock contention in
      getNextAudioBlock), and last offline render speed (x realtime). Instrumented with
      lock-free relaxed atomics on the audio thread + timing in apiRenderToFile — the
      audio path stays lock-free. Python `diagnostics()`; smoke logs it. Verified:
      44100/512/2in/2out, render ~130-180x realtime, DSP load ~0.2%. **Not yet:**
      per-plugin latency, xrun marks on the timeline, allocation/lock debug asserts, a
      big transport/record status view.

15. **Selective built-in effects & analyzers.** **M** (curated — see principle 5)
    *Idea #8/#13.* Fill obvious gaps only: parametric EQ, compressor (have limiter),
    bitcrusher, chorus/flanger, stereo widener, waveshaper/soft-clipper; plus
    oscilloscope / spectrum / vectorscope as **non-mutating** inserts exposing
    API-visible analyzer snapshots. Each registers in `Effects.h` `makeEffect` + the
    `EffectType` proto enum, and works in effect-chain presets.
    *Done when:* each effect renders its expected transform; an analyzer snapshot is
    retrievable over the API.
    - `[x]` **Bitcrusher + Compressor landed** (`Source/Effects.h`, commit): two
      Effect subclasses. Bitcrusher = bit-depth reduction + sample-rate decimation +
      wet/dry (Bits/Downsample/Mix); Compressor = peak-detected soft-knee with
      Thresh/Ratio/Attack/Release/Makeup. Registered in `EffectFactory::create` +
      `types()`, the proto `EffectType` enum (BITCRUSHER=5, COMPRESSOR=6), and
      `apiAddEffect`'s names[]; params flow through the existing `EffectParam`
      interface so GetEffectParams / effect presets / ParamModel / automation /
      modulation all work for free. Verified via render: bitcrusher quantizes to ~5
      distinct sample values; compressor is level-dependent (loud note ducked 10 dB vs
      quiet 6 dB — dynamic range squeezed). smoke covers both.
    - `[x]` **Parametric EQ + Waveshaper landed** (commit): `EqFx` (single peaking
      band, RBJ biquad, Freq/Gain/Q) and `WaveshaperFx` (tanh soft-clip Drive + Mix),
      as EffectType EQ=7 / WAVESHAPER=8 (all four registries kept in sync). Verified via
      render: EQ ±18 dB @ 500 Hz shifts band RMS ~17 dB (boost vs cut); waveshaper drive
      25 raises RMS ~17 dB (saturation). smoke covers both.
    - `[x]` **EQ upgraded to 3 bands** (commit): `EqFx` is now a low shelf + mid peak +
      high shelf chained per channel (Low Freq/Low dB, the original Freq/Gain dB/Q mid,
      High Freq/High dB — 7 params). **Backward-compatible**: the mid band keeps the old
      param names so projects that stored the single-band EQ still load, with the new
      shelves defaulting flat. The RBJ peak/low-shelf/high-shelf coefficient math moved to a
      pure header `Source/Biquad.h` (with a transfer-function magnitude evaluator),
      unit-tested (`GloopyTests::BiquadEq`: each shelf hits its target gain in-band and is
      flat out-of-band; 0 dB is unity). smoke proves both shelves shift RMS (added a bright
      saw track so the master has HF content for the high shelf); the 7 knobs auto-render in
      the generic FX param panel (screenshot-validated). No proto change (effects serialise
      generically by param name).
    - `[x]` **Tempo-synced delay** (commit): effects can now be tempo-aware — added a
      `Effect::setTempo(bpm)` hook, called each block for every insert/master effect with
      `transport.getBpm()`. `DelayFx` gained a "Sync bt" param: 0 = free (Time ms), else the
      delay length in beats (0.5 = 1/8, 1 = 1/4, ...), converted to ms via the block tempo.
      smoke proves it exactly: a synced 1/4 @120 BPM is a byte-for-byte match of a free 500 ms
      delay (diff 0.00000, re-adding a fresh delay per render so feedback tails don't bleed
      across renders — a gotcha), and a tempo change to 240 BPM halves it (render differs).
      The "Sync bt" knob auto-renders in the FX panel (screenshot-validated). The setTempo hook
      generalises to future tempo-synced effects.
    - `[x]` **Tempo-synced modulation effects** (commit): Chorus/Flanger/Phaser gained the same
      "Sync bt" param (LFO cycle length in beats; 0 = free Hz) via the setTempo hook. The
      sync->rate math is a pure header (`Source/EffectSync.h`, `effectSyncedRate(bpm,
      syncBeats, freeRate) = bpm/(60*beats)` clamped), unit-tested (`GloopyTests::EffectSync`).
      The chorus smoke now proves tempo-sync EXACTLY (Sync 1bt@120 == free 2 Hz, diff 0.00000)
      once the reproducible-bounce fix below landed. **Not yet:** dotted/triplet sync labels.
    - `[x]` **Reproducible offline bounces (effect reset) landed** (commit): `apiRenderToFile`
      now calls `fx->reset()` on every insert/master effect before rendering (next to the
      existing `resetModulationSmoothing`), so a bounce is bit-identical run-to-run. Fixes a
      real pre-existing bug found while testing tempo-synced effects: modulation effects
      (delay/chorus/flanger/phaser) carried their delay-line + LFO-phase state across renders,
      so two renders of the same project differed (~0.026) — non-deterministic, violating the
      composition-as-repo "reproducible render" principle. Verified: chorus, and chorus + a
      0.7-feedback ping-pong delay (worst case), now render byte-identical twice (diff 0.0);
      the tempo-synced-chorus smoke asserts sync==sync2 and the exact free-vs-synced match.
    - `[x]` **Stereo Widener landed** (commit): `StereoWidenerFx`, mid/side, one Width
      param (0 mono / 1 unchanged / 2 double-wide), as EffectType STEREO_WIDENER=9 (all
      four registries synced). The pure transform lives in `Source/StereoWiden.h`
      (`widenSample`) so it's unit-tested without the audio-processor dep — `GloopyTests::
      StereoWidener` proves width 0/1/2 side scaling, mono-sum invariance, and that a
      mono signal is untouched; smoke proves the enum→factory→Width-param wiring. The
      mixer add-effect menu is data-driven from `EffectFactory::types()`, so the desktop
      affordance is automatic.
    - `[x]` **Chorus landed** (commit): `ChorusFx`, a short LFO-modulated delay (base
      12 ms, sweep depth ms) with no feedback, dry/wet Mix, and a quarter-cycle L/R LFO
      offset for width. Rate/Depth/Mix params; EffectType CHORUS=10 (four registries
      synced); auto-listed in the data-driven mixer menu. smoke proves the enum→factory
      →Mix wiring: Mix=0 is a bit-exact passthrough of the dry render, Mix=0.8 audibly
      differs (mean|Δ|=0.08).
    - `[x]` **Flanger landed** (commit): `FlangerFx`, a very short LFO-swept delay
      (0.5 ms centre, ≤5 ms depth) **with feedback** — a resonant comb "jet" sweep,
      unlike the feedback-free chorus. Rate/Depth/Feedbk/Mix params; EffectType
      FLANGER=11 (four registries synced); auto-listed in the mixer menu. smoke proves
      the enum→factory + Feedbk/Mix params: Mix=0 bit-exact passthrough, Mix=0.8 active
      (mean|Δ|=0.10). The chorus/flanger backlog pair is done.
    - `[x]` **Phaser landed** (commit): `PhaserFx`, a cascade of 6 LFO-swept first-order
      allpass stages (log sweep ~200 Hz..up to 5 octaves) with feedback and dry/wet Mix —
      sweeping notches, completing the modulation-effects family (chorus/flanger/phaser).
      Rate/Depth/Feedbk/Mix params; EffectType PHASER=12 (all four registries synced — proto
      enum, `types()`, `create()`, the `apiAddEffect` names[]; also back-filled the stale
      Python EFFECTS map which had stopped at WAVESHAPER=8). The pure DSP is
      `Source/AllpassPhaser.h` (`allpassStage`/`phaserCoeff`), unit-tested
      (`GloopyTests::AllpassPhaser`: coefficient range/monotonicity + the allpass
      magnitude-preservation property across a 6-stage cascade) without the audio-processor
      dep. smoke proves the enum→factory→params wiring (waveform diff 0.13 vs dry, stays
      level-matched within ~1.4 dB); screenshot-validated (Phaser in the add-effect menu).
    - `[x]` **Tremolo landed** (commit): `TremoloFx`, periodic amplitude modulation — a sine
      LFO scales the gain between 1 and (1 - Depth) at Rate Hz, or tempo-synced when Sync bt > 0
      (reusing the shared `effectSyncedRate` beats→Hz law from the modulation effects + the
      `setTempo` hook). Both channels share one phase (classic tremolo, not auto-pan); Depth 0
      short-circuits to a true identity; reset() zeroes the phase so bounces are reproducible.
      Rate/Sync bt/Depth params; EffectType TREMOLO=10 (all four registries synced by INSERTING
      at index 10 — proto enum, `types()`, `create()`, `apiAddEffect` names[], Python EFFECTS —
      which shifted CHORUS/FLANGER/PHASER to 11/12/13; safe because effects serialise by name and
      the desktop menu adds by string, so the enum is only an AddEffect input with no reverse
      lookup). smoke proves depth 0 = bit-exact identity, depth 1 swings windowed RMS ~40× (a
      near-silent trough), and a synced 1-beat cycle == a free LFO at bpm/60 Hz byte-for-byte;
      screenshot-validated (Tremolo in the add-effect menu + its 3 auto-rendered knobs).
    - `[x]` **Auto-pan landed** (commit): `AutoPanFx`, the stereo companion to the tremolo — one
      sine LFO drives the two channels in ANTIPHASE (L gain = 1 - Depth·(0.5+0.5·sin), R uses
      -sin), sweeping the signal L↔R at Rate Hz or tempo-synced (Sync bt), reusing
      `effectSyncedRate` + `setTempo`. Depth 0 = identity; Depth 1 = full ping-pong; mono passes
      through (needs a stereo buffer); reset() zeroes the phase. Rate/Sync bt/Depth params;
      EffectType AUTOPAN=14 — this time **appended** at the end of all four registries (proto enum,
      `types()`, `create()`, names[], Python EFFECTS) so no existing enum values shifted. smoke
      proves depth 0 = bit-exact identity, depth 1 sweeps L↔R (a window where L hard-pans AND one
      where R hard-pans, each >3× the other channel), and synced(1bt)==free(bpm/60 Hz) byte-for-
      byte; screenshot-validated (Auto-pan in the add-effect menu + its 3 knobs). Completes the
      tremolo/auto-pan amplitude-LFO pair.
    - `[x]` **Noise Gate landed** (commit): `NoiseGateFx`, a downward gate — the dynamics
      complement to the limiter/compressor. Below Thresh dB the gate gain falls to a Range dB
      floor (default -60 ≈ silence), attenuating hiss/bleed in the gaps; above threshold it
      passes at unity, with Attack (opening) / Release (closing) smoothing. Starts closed;
      reset() clears state for reproducible bounces. Thresh/Range/Attack/Release params;
      EffectType NOISE_GATE=15, **appended** across all four registries (no enum shift). smoke
      renders a percussive note (loud attack, quiet sustain) through the gate: the loud attack
      passes (head -1.4 dB vs dry) while the quiet tail is gated (tail -60 dB); screenshot-
      validated (Noise Gate in the add-effect menu + its 4 knobs). Completes the
      limiter/compressor/gate dynamics trio.
    - `[x]` **Auto-wah landed** (commit): `AutoWahFx`, an envelope-following resonant low-pass
      (the classic funk "envelope filter"). A peak follower on the input drives the cutoff
      `fc = Base Hz · 2^(Range·env)`, so a loud transient snaps the filter open and it closes as
      the note decays. Base Hz / Range oct / Q / Release ms params; a shared mono detector sweeps
      both channels' TPT-SVF filters; reset() clears state. EffectType AUTOWAH=16, **appended**
      across all four registries (no enum shift). smoke exploits that a saw's zero-crossing rate
      is level-INDEPENDENT: a LOUD tone through the auto-wah renders brighter (ZCR 1279) than a
      QUIET one (853) — a static filter would filter both identically, so this directly proves
      the envelope drives the cutoff; also reproducible. Screenshot-validated (Auto-wah in the
      add-effect menu + its 4 knobs). Distinct from the LFO effects (tremolo/auto-pan) and the
      dynamics (limiter/compressor/gate) — a dynamic *filter*.
    - `[x]` **Ring Mod landed** (commit): `RingModFx`, a nonlinear amplitude multiplication —
      `out = in·sin(2π·Freq·t)` blended by Mix — producing inharmonic sum/difference tones
      (metallic/bell/robotic). Freq / Mix params; a shared carrier phase across channels;
      reset() zeroes it. EffectType RINGMOD=17, **appended** across all four registries (no enum
      shift). smoke exploits the level-independent ZCR: a LOW ~98 Hz sine multiplied by a 1500 Hz
      carrier shifts energy up to ~1400/1600 Hz, so the wet ZCR jumps ~16× (6138 vs 380); Mix 0 is
      a bit-exact identity and the render is reproducible. Screenshot-validated (Ring Mod in the
      add-effect menu + its 2 knobs). Distinct from every prior effect (LFO / dynamics / filter) —
      a nonlinear multiplier.
      **Not yet:** analyzers (scope/spectrum/vectorscope) with API snapshots.

### Wave 6 — Product surface & UI (deferred: harder to verify headless; keep layout simple)

These are lower priority precisely because they're GUI-heavy — build them *on top of*
the API surface the earlier waves harden, so they stay thin clients. Finish the
deferred preset work (sampler + plugin instrument presets, plugin effects in
effect-chain presets) as part of #16.

**UI-PARITY BACKLOG (per the "desktop UI for every feature" rule — 2026-07 audit).**
An audit of all ~106 RPCs found features that shipped API-only. Close each with a
desktop control wiring the *same* api* op, screenshot-validated. Status:
- `[x]` **Chord tool** — chord-type combo in the editor header; click grid to stamp
  (commit d024a52).
- `[x]` **Clip ops split/duplicate/reverse/snap-to-scale** — right-click arrange-view
  clip context menu → routes to apiSplitClip/apiDuplicateClip/apiReverseClip/
  apiSnapClipToScale (commit 9f33668).
- `[x]` **Project scale selector** (SetScale/GetScale) — toolbar root + scale-name combos
  wired to apiSetScale; drives the piano-roll highlight + snap-to-scale; refreshes from
  the model on load (commit). Screenshot-validated (dorian tint + GetScale readback).
- `[x]` **Modulation / LFO** (SetModulation/RemoveModulation) — Add LFO... / Remove LFO on
  the mixer param right-click menu (rate/depth/shape prompt; centre = current value)
  (commit efac838). Screenshot + headless validated.
- `[x]` **MIDI-learn** (AddControllerMap/MidiLearn) — right-click a mixer fader / pan /
  FX param knob → MIDI Learn → apiMidiLearn(target) (commit 8567627). Screenshot +
  headless validated (cc:20 → insert/0/volume).
- `[x]` **Mappings rack VIEW landed** (commit) — the mod-matrix/mapping-rack follow-up
  (#19). A "Maps" toolbar button opens a Mappings window (`Source/MappingsView.h`, a thin
  client) that lists EVERY control mapping in one place: each MIDI/OSC controller map
  (`source -> target [lo..hi]`, "(bypassed)" when off) and each LFO route (`LFO (shape,
  rate/sync, depth) -> target`), each with a Remove button wiring apiRemoveControllerMap /
  apiRemoveModulation, then re-querying. Pure UI over the existing (already smoke-tested)
  apiListControllerMaps/apiListModulations ops — no new RPC. Screenshot- AND functionally-
  validated: added a controller map + an LFO via gRPC, opened the window (both rows shown),
  clicked Remove -> ListControllerMaps empty while the LFO route persists and the list
  rebuilt. Still open: per-map edit (range/inversion/smoothing) in the rack.
- `[x]` **Tempo markers** (AddTempoMarker/RemoveTempoMarker) — right-click the beat ruler
  → Add (BPM prompt) / Remove; markers drawn as flags on the ruler (commit 004c19f).
  Screenshot + headless validated (bar 3 / 150 BPM).
- `[x]` **Punch range** (SetPunchRange) — Alt-drag the ruler to set the punch in/out
  region; drawn in red (loop stays plain-drag/accent) (commit 71e31ef). Screenshot-
  validated (RPC-set bars 2-4 + Alt-drag bars 5-7).
- `[x]` **Swing groove UI** (SetSwing) — the engine already swung 1/8 notes (`transport.swing`,
  used in `collectClip`) with an `apiSetSwing`/SetSwing RPC + serialisation, but had **no
  desktop control** and wasn't readable. Added a "Swing ▸" submenu on the beat-ruler
  right-click menu (Straight / Light 56% / Medium 62% / Heavy 68% / Triplet 67%, ticked at
  the current value) and a `swing` field on `GetTransport`/`TransportState` (+ Python
  `transport()` readback). Screenshot-validated; smoke proves swing shifts off-beat 8ths
  (render diff) and the amount round-trips via GetTransport.
- `[x]` **Metronome / click track** (SetMetronome) — a beat click generated in `renderBlock`
  (a 30 ms decaying sine per beat, higher/louder accent on bar downbeats, time-signature
  aware; allocation-free, click state persists across blocks). A monitor layer on top of the
  master that also lands in an offline bounce if left on. `GetTransport`/`TransportState`
  gained a `metronome` flag (+ the `loop_*` fields from the loop-to-clip slice). SetMetronome
  RPC + Python + `transport()` readback. **Desktop UI:** a "Metro" toolbar toggle next to Loop
  — screenshot-validated (highlights when on). smoke proves an empty 4-beat render is silent
  off and clicks each beat on. Session toggle (not serialised).
  - `[x]` **Metronome level landed** (commit): the click volume was fixed (accent 0.6 / beat 0.4);
    now a `metronomeLevel` (0..1, session setting) scales it. `apiSetMetronomeLevel`/
    `apiGetMetronomeLevel` + SetMetronomeLevel/GetMetronomeLevel RPCs (`MetronomeLevel{level}`) +
    Python. **Desktop:** a "Metronome level ▸ 25/50/75/100%" submenu on the beat-ruler right-click
    menu (alongside Swing/Time signature), reflecting + setting the level. smoke proves level 0.5
    halves the click peak (0.60→0.30) and round-trips via GetMetronomeLevel; screenshot-validated
    (the expanded submenu, 100% checked).
- `[x]` **Take management** (PromoteTake/CleanupTakes) — right-click a recorded take clip
  → Use this take (comp: unmute this, mute siblings) / Promote take (keep) / Clean up
  unused takes (commit 805e4f0). Validated with a real loop+punch recording harness
  (6 takes → promote moved raw→recordings, cleanup deleted an unreferenced file).
- `[x]` **MIDI file import** (ImportMidi) — `File → Import MIDI File...` launches a
  `.mid/.midi` chooser and calls apiImportMidi (synth track + clip per MIDI track),
  the same op as the RPC (commit). Was API/CLI-only (Wave 2 #5 shipped the backend
  without a desktop path). Screenshot-validated (menu item present on Xvfb); the
  import op itself is smoke-proven. Fast-follow: drag-and-drop a `.mid` onto the
  arrange view (needs a FileDragAndDropTarget + drop-position → track/time mapping).
- `[x]` **Audio file import API** (ImportAudio) — the *reverse* parity gap: audio
  import was **GUI-only** (the `+ Audio` button), unlike MIDI which had an RPC.
  Extracted the button's inline decode into `apiImportAudio(path)` (wav/aiff/flac →
  a new audio track + clip at bar 1; decode off-thread, insert on the message thread)
  and exposed it as the `ImportAudio` RPC + Python `import_audio`; the button now
  shares it (commit). Headless-proven in smoke.sh (imports a rendered WAV → +1 audio
  track, rejects a non-audio file). Enables `.wav` drag-and-drop as a later slice.
- `[x]` **Drag-and-drop files onto the window** — `MainComponent` is now a
  `FileDragAndDropTarget`; a pure `classifyDroppedFile` (`Source/FileDrop.h`) routes
  each dropped path to the already-tested op: `.gloopy/.zip`/composition-dir →
  openAny, `.mid/.midi` → apiImportMidi, `.wav/.aif/.aiff/.flac` → apiImportAudio
  (commit). The classifier is unit-tested (`FileDropTests`, extension + directory
  cases) so the routing is headless-verified even though the OS drop event isn't
  scriptable; the load ops themselves are smoke-proven. Closes the MIDI/audio
  drag-drop fast-follows in one shared dispatcher.

**UI-PARITY BACKLOG COMPLETE — all 11 audited API-only gaps + MIDI-import have desktop UI.**
Next: Wave 6 Tier-1 piano-roll items (strum, arpeggiate, snap-to-scale-while-drawing),
each shipping with desktop UI + screenshot validation.

- `[x]` **Rename track landed** (commit): closed a real UX gap — a track's name was fixed at
  creation with no way to change it. `apiRenameTrack(id,name)` (message-thread, rejects
  empty/whitespace, pushes undo) + RenameTrack RPC (`RenameTrackRequest`) + Python
  `rename_track`. **Desktop:** the track-header right-click now opens a track menu with "Rename
  track..." (a name-prompt AlertWindow) — and, for sampler tracks, the existing "Sampler
  playback window..." (the header popup was previously sampler-only; it's now a general track
  menu). Name already serialises, so it round-trips. smoke: create renme-orig → rename →
  GetState shows renme-new (not the old name) and it survives a SaveComposition/NewProject/
  LoadComposition round-trip (wrapped in a SaveProject/LoadProject session snapshot so the
  destructive reload doesn't disturb later blocks). Screenshot-validated end-to-end: header
  menu → dialog (prefilled "Kick") → typed "Boom" → the track header updates live to "Boom".
- `[x]` **Track colour landed** (commit): the per-track `colour` already serialised but was
  auto-assigned at creation with no way to change it. `apiSetTrackColour(id,hexArgb)` (parses
  8-hex ARGB, message-thread, undo) + SetTrackColour RPC (`SetTrackColourRequest`) + Python
  `set_track_colour`; the colour is now also exposed on `TrackInfo.colour` (GetState/ListTracks
  + Python `list_tracks`) so it's inspectable. **Desktop:** a "Colour ▸ Red/Orange/Yellow/Green/
  Teal/Blue/Purple/Grey" preset palette on the track-header menu (next to Rename). smoke folds
  it into the rename block (set purple ffab47bc → survives the composition round-trip in
  GetState); screenshot-validated end-to-end (picked Green → the Kick clip + header accent turn
  green live).
- `[x]` **Move track up/down landed** (commit): reorder a track in the arrangement.
  `apiMoveTrack(id,delta)` (delta<0 up toward the top, >0 down) swaps adjacent entries in the
  `tracks` vector under the lock — safe because each track's `mixerTrack` index lives ON the
  Track, so routing/inserts are unaffected by vector order (and serialization writes/reads the
  new order). Message-thread, undo, no-op at an edge. MoveTrack RPC (`MoveTrackRequest`) + Python
  `move_track`. **Desktop:** "Move up"/"Move down" on the track-header menu, disabled at the
  respective edge. smoke: append mvA/mvB/mvC, move mvC up x2 → GetState order mvC/mvA/mvB, then
  remove the trio. Screenshot-validated end-to-end (Kick's menu shows Move-up disabled at the
  top; picking Move down swapped Kick below Snare, each keeping its clip/colour).
  **Deferred (needs its own tick):** *Duplicate track* — a true clone needs the per-track
  save/load factored into a reusable helper AND its own mixer strip (each track gets a distinct
  `mixerTrack` strip, so a naive clone would share inserts/fader); not a clean single-tick slice.
  **ATTEMPTED + REVERTED (crash):** the "clone the TRACK subtree in toValueTree() and reload via
  loadFromTree()" shortcut CRASHES — first call silently dropped the clone's notes, a second gRPC
  call then killed the server (EOF/connection-refused). loadFromTree() from inside a
  callOnMessageThread api* lambda (mutating the just-built tree + full reload) is not safe. Do NOT
  use the whole-project-reload approach. The right path remains: extract a `readTrackFromTree`
  helper (loader lines ~3706-3871) so a single Track can be built without reloading the project,
  then add + wire its own mixer strip. Reverted cleanly; no partial code left in tree.
- `[x]` **Rename clip landed** (commit): a clip's label was fixed at creation. `apiRenameClip(track,
  index,name)` sets `clip.name` (message-thread, undo; empty name → the label falls back to the
  track name, which `drawClip` already does) + RenameClip RPC (`RenameClipRequest`) + Python
  `rename_clip`; `clip.name` already serialises. **Desktop:** a "Rename clip..." item on the
  MIDI/audio clip menu → a name-prompt dialog prefilled with the clip's current name (via a new
  ArrangeView `onRenameClip` hook). smoke: rename a clip to "Chorus" → SaveProject writes
  `name="Chorus"` into the .gloopy. Screenshot-validated end-to-end (clip menu → dialog → typed
  "Verse" → the clip label updates live from "Kick" to "Verse" while the track header stays "Kick").
- `[x]` **Per-clip colour override landed** (commit): a `Clip.colour` (ARGB 0 = inherit the track
  colour) drawn by `drawClip` (`c.colour.getARGB() ? c.colour : t.colour`), so a clip can be
  colour-coded independently of its track (mark sections). `apiSetClipColour(track,index,hexArgb)`
  ("" clears → inherit; message-thread, undo) + SetClipColour RPC (`ClipColourRequest`) + Python
  `set_clip_colour`. Serialised on the CLIP ValueTree (`colour`, omitted when inheriting).
  **Desktop:** a "Colour ▸ Red/…/Grey + Inherit track" submenu on the clip menu (routed via
  onClipCommand "clipcolour:<hex>"). smoke: set a clip red → one extra `colour=` attr in the saved
  .gloopy, clearing removes it (delta check, since other tracks/clips also carry colours; juce
  wraps long XML tags across lines so a per-tag single-line grep won't work — count occurrences).
  Screenshot-validated end-to-end (Snare clip menu → Colour → Purple → the clip draws purple while
  its track-header stripe stays yellow).
- `[x]` **Track polarity / phase invert landed** (commit): a per-track `polarity` flag negates
  the track's contribution as it's summed into its mixer strip (`v *= pol` at the pan-law addFrom)
  — a standard channel polarity/phase button, useful to cancel a correlated layer. `apiSetTrackPolarity(id,invert)`
  (atomic, message-thread, undo) + SetTrackPolarity RPC (`SetTrackPolarityRequest`) + Python
  `set_track_polarity`; also surfaced on `TrackInfo.polarity` (GetState/ListTracks + `list_tracks`).
  Serialised on the TRACK ValueTree (`polarity`, omitted when off). **Desktop:** a checkable "Invert
  phase" item on the track-header menu (reflects + toggles the flag). smoke is a decisive
  cancellation proof — two IDENTICAL synth tracks (SynthVoice resets phase to 0 on note-on, so they're
  sample-identical) sum LOUD (peak 0.41), then inverting one renders near-silent (peak 0.00000);
  polarity also survives a SaveProject/LoadProject round-trip (GetState). Screenshot-validated
  end-to-end (menu shows "Invert phase" → after clicking, re-opening shows it ✓ checked).
- `[x]` **Desktop audio export landed** (commit): closed a real gap — audio bounce was API/CLI
  only (the File menu had "Export MIDI..." but NO audio export). Added File → "Export Audio
  (WAV)..." (whole mix) and "Export Loop Region (WAV)..." (enabled only when a loop is set), both
  rendering off the message thread via `runBackground` + the busy overlay (a long bounce no longer
  freezes the UI; apiRenderToFile holds the engine lock and is thread-safe off-message). New
  `apiExportLoopRegion(path)` bounces the current transport loop `[start,end)` (fails if no
  loop / empty) — reuses apiRenderToFile; ExportLoopRegion RPC (`FilePath`) + Python
  `export_loop_region`. Both choosers accept `.wav`/`.flac` (encoder from extension). smoke: set a
  2-beat loop, ExportLoopRegion → a valid WAV exactly half the full-mix render (85444 < 170888
  frames). Screenshot-validated (File menu shows both items, loop-region enabled with the loop on).
- `[x]` **Per-track stem export landed** (commit): completes the desktop export trio (mix / loop /
  stem). `apiExportTrack(id,path)` bounces ONE track through its own insert chain, soloed, to a
  WAV/FLAC (reuses apiRenderToFile's single-soloed-track path). ExportTrack RPC
  (`ExportTrackRequest`) + Python `export_track`. **Desktop:** "Export track (WAV)..." on the
  track-header menu → FileChooser (defaults to the track name under ~/Music) → renders off the
  message thread via runBackground + busy overlay. smoke: a track with a note bounces non-silent
  (0.205) while an EMPTY track's stem is silent (0.00000) — proving the stem is isolated to its
  target, not the whole mix. Screenshot-validated (the menu item is present).
- `[x]` **Export stems (all tracks) — desktop + API landed** (commit): factored the CLI
  export-stems loop into `apiExportStems(dir)` (bounces every instrument track to `<id>-<slug>.wav`
  via apiExportTrack; returns the file list) and reused it in `gloopy export-stems` (no behavior
  change — CLI smoke still green). New ExportStems RPC (`FilePath` dir → Ack, ok if ≥1 stem) +
  Python `export_stems`. **Desktop:** File → "Export Stems (WAV)..." → folder chooser → renders off
  the message thread via runBackground, then reports the count. smoke: 2 note-bearing synth tracks →
  ExportStems → 2 WAVs on disk. Screenshot-validated (the File-menu item, completing the export
  group: MIDI / Audio / Loop Region / Stems).
- `[x]` **Rename mixer strip landed** (commit): first direct MixerView slice — mixer strips were
  labelled generically ("Ins N") with no way to rename them. `apiSetInsertName(index,name)`
  (message-thread, undo, rejects empty) + SetInsertName RPC (`InsertName`) + Python
  `set_insert_name`; the MTRACK `name` already serialised so it round-trips. **Desktop:** a "Rename
  strip..." item on the mixer strip-name right-click menu (the same menu as control groups / sends),
  opening a name-prompt AlertWindow prefilled from the strip's current name (MixerView holds the
  mixerTracks ref + engineLock). smoke: rename the highest-index strip → ListInserts shows "DrumBus"
  and it survives a composition round-trip. Screenshot-validated end-to-end (strip menu → "Rename
  strip..." → dialog → typed "DrumBus" → the strip header updates live from "Ins 1" to "DrumBus").
- `[x]` **Partial / strength quantize landed** (commit): `quantizeNotes` gained a `strength` (0..1)
  arg — each note moves `strength` of the way toward the nearest grid line (1 = full snap, the
  default and existing behavior; 0.5 = halfway = tighten-without-robotizing; 0 = no move). Existing
  callers (PianoRoll Q, apiQuantizeClip) are unchanged (default 1.0). `apiQuantizeClip` +
  QuantizeRequest gained `strength` (handler defaults unset 0 → full, back-compat). Python
  `quantize_clip(strength=1.0)`. **Desktop:** a "Quantize ▸ 1/16 · 1/16 soft (50%) · 1/8 · 1/8 soft
  · 1/4 · 1/4 soft" submenu on the MIDI-clip menu (routed via onClipCommand "quantize:grid,strength").
  `GloopyTests::NoteEdits` pins the partial move (0.1@grid0.25 → 0.05 at 50%, 0.0 at full, 0.1 at 0);
  smoke drives QuantizeClip 50% → GetClipNotes 0.05; screenshot-validated (the expanded submenu).
- `[x]` **Note-length gate (staccato/tenuto) landed** (commit): a shared `gateNotes(notes,factor)`
  transform scales every note's LENGTH by `factor` keeping its start — 0.5 = staccato (detached),
  1.5 = tenuto (fuller). Distinct from legato (stretch to the NEXT onset) and time-scale (scales
  start AND length): gate reshapes only articulation, rhythm unchanged. `apiGateClip` (via
  GLOOPY_EDIT_CLIP_NOTES) + GateClip RPC (`GateRequest`; handler defaults unset 0 → 1.0 no-op) +
  Python `gate_clip`. **Desktop:** a "Note length ▸ Staccato (50%) / Short (75%) / Tenuto (150%) /
  Double (200%)" submenu on the MIDI-clip menu. `GloopyTests::NoteEdits` pins the length scale with
  starts kept; smoke drives GateClip 0.5 → GetClipNotes len 0.5 start 0.5; screenshot-validated.
- `[x]` **Flatten velocity landed** (commit): a shared `flattenVelocities(notes,value)` transform
  sets every note's velocity to one value (0..1) — make a part mechanically even / reset dynamics
  before re-shaping. Distinct from velocity-scale (multiply), ramp (gradient) and humanize (jitter):
  flatten makes them all identical. `apiFlattenClipVelocity` (via GLOOPY_EDIT_CLIP_NOTES) +
  FlattenClipVelocity RPC (`FlattenVelRequest`) + Python `flatten_clip_velocity`. **Desktop:** a
  "Flatten velocity ▸ Flat 100%/75%/50%/25%" submenu on the MIDI-clip menu. `GloopyTests::NoteEdits`
  pins it (0.3/0.9/0.6 → all 0.5, pitch/timing kept, out-of-range clamped); smoke drives
  FlattenClipVelocity 0.5 → GetClipNotes velocities all 0.5; screenshot-validated (the submenu).
- `[x]` **Duplicate track landed** (commit) — the deferred high-value item, done safely. Extracted
  the loader's per-track block into `buildTrackFromTree(ValueTree) -> unique_ptr<Track>` (a
  MECHANICAL move via a brace-matching python script — no hand transcription; the full smoke suite,
  which load/saves projects everywhere, proved it behavior-identical). `apiDuplicateTrack(id)` then
  clones the source's TRACK subtree in `toValueTree()`, drops its `tid`, appends " copy" to the
  name, builds ONE track via the helper and `addTrack`s it (which assigns the clone its own mixer
  strip + a fresh id) — **no full `loadFromTree` reload** (that was the crash last attempt).
  DuplicateTrack RPC (TrackId->TrackId) + Python `duplicate_track`. **Desktop:** "Duplicate track"
  on the track-header menu. smoke: a synth track with a 2-note clip -> "dupsrc copy" with clips=1,
  its clip carries notes 60/64, AND a follow-up GetClipNotes succeeds (the old reload approach
  dropped the notes then crashed the server on the next call — both now fixed). Screenshot-validated
  (right-click Lead -> Duplicate track -> a "Lead copy" track appears). Follow-up: the clone gets a
  fresh EMPTY mixer strip (its inserts aren't copied) — copying the source's insert chain is a later
  slice.
- `[x]` **Split clip into N equal pieces landed** (commit): a loop-chopper. `apiSplitClipEqual(track,
  index,pieces)` reads the clip's [start,len), then walks apiSplitClip at each interior boundary —
  splitting the freshly-cut RIGHT remainder each time (apiSplitClip inserts the right half at
  index+1 and returns it), so notes/audio distribute correctly. Clamped 2..32; returns pieces made.
  SplitClipEqual RPC (`SplitEqualRequest` -> SliceResult) + Python `split_clip_equal`. **Desktop:** a
  "Split into ▸ 2/4/8/16" submenu near "Split at playhead" on the clip menu. smoke: a 4-beat clip
  (notes 0/1/2/3) chopped into 4 -> made=4 and the track has 4 clips. Screenshot-validated (the Kick
  clip became four one-beat clips while other tracks stayed intact).

16. **Browser sidebar + demo/template browser + `File → New From Template`** *(Idea
    #1/#2; absorbs remaining preset UI/work).* **L**
    - `[~]` **Empty-by-default + template system landed** (commit 5b90d3b, user request):
      new/launched projects are EMPTY (the forced Kick/Snare/Hat/Clap/Bass kit is gone).
      `buildTemplate`/`builtinTemplateNames` (Starter Beat / Drum Kit / Lead + Bass) +
      apiListTemplates/apiNewFromTemplate + ListTemplates/NewFromTemplate RPCs + Python +
      `File → New from Template`. Track ids are now 1-based (proto3 omits id 0). **The
      browser sidebar should drive project seeding through this same template API.**
    - `[~]` **Browser sidebar — first slice landed** (commit, user request "tackle the browser
      sidebar next"): a docked, collapsible LEFT panel (`Source/BrowserSidebar.h`, header-only) with
      a "TEMPLATES" category — each built-in/user template (from `apiListTemplates`) is a clickable
      row that seeds a new project via `apiNewFromTemplate` (off-thread + busy overlay), exactly the
      "drive seeding through the template API" the note above asked for. Toggled by a compact "☰"
      toolbar button (`browseButton`, next to Maps/Mixer — kept narrow so the crowded toolbar didn't
      push Metro off, a real overflow gotcha); when shown it docks 210px on the left of the main
      content and `resized()` reflows arrange/editor. Screenshot-validated end-to-end (☰ → the
      TEMPLATES list [Starter Beat / Piano+Bass+Drums / Drum Kit / Lead+Bass] → clicking "Starter
      Beat" seeded the Kick/Snare/Hat/Clap/Bass tracks with the sidebar still open).
    - `[~]` **Browser sidebar — Demos tab landed** (commit): the sidebar is now TABBED
      (`setCategories({...})` — each `Category` = title + getItems + onChoose). Added a "Demos"
      category listing the bundled example compositions: `demosDir()` resolves `$GLOOPY_EXAMPLES_PATH`
      else an `examples/` folder by the CWD/executable; `listDemos()` returns the composition-folder
      names (dirs holding a `gloopy.toml`), and clicking one `openAny`s it (off-thread + busy overlay).
      Screenshot-validated end-to-end (Templates|Demos tabs; the Demos tab listed the 8 bundled
      demos [demo-ambient … demo-synthwave]; clicking "demo-song" loaded the full 7-track composition
      at tempo 124). **Next slices:** Samples / Plugins / Presets / Favorites tabs, and first-class
      drag-and-drop from the browser onto tracks/inserts.
    - `[~]` **Browser sidebar — Plugins tab landed** (commit): a third category listing every
      installed INSTRUMENT plugin (`apiListPlugins()` filtered to `isInstrument`, label "name
      (format)"); clicking one `apiAddPluginTrack(identifier)`s it (off-thread + busy overlay). Row
      label → identifier is cached in `browserPluginIds` (getItems fills it; onChoose looks it up,
      since a Category's onChoose only gets the label). apiListPlugins loads the plugin cache on
      first use, so the tab populates without a manual rescan. Screenshot-validated end-to-end with
      the real host: the Plugins tab listed sfizz / Surge XT (VST3+LV2) / ZynAddSubFX / Calf * , and
      clicking "Surge XT (LV2)" instantiated it as a new "Surge XT" instrument track. **Next:** Samples
      / Presets / Favorites tabs, and first-class drag-and-drop onto tracks/inserts.
    - `[~]` **Browser sidebar — Samples tab landed** (commit): a fourth category listing importable
      audio files (wav/aiff/flac) under `samplesDir()` — resolved from `$GLOOPY_SAMPLES_PATH`, else a
      `samples/` folder next to the CWD, else `~/Music` (the demosDir resolver pattern). Clicking a row
      `apiImportAudio`s the file as a new audio track (off-thread + busy overlay). Pure desktop control
      over the existing import path (no new RPC — import is already smoke-proven). Screenshot-validated
      end-to-end: pointed `$GLOOPY_SAMPLES_PATH` at two rendered WAVs (bass-220 / tone-440), the tab
      listed both, and clicking tone-440.wav created an AUDIO track "tone-440" with its clip at bar 1.
      **Next:** Presets / Favorites tabs, and first-class drag-and-drop onto tracks/inserts (the
      "first-class" item the ideas doc emphasizes: sample→sampler/audio track, plugin→track/insert).
    - `[x]` **User templates ("Save as Template") landed** (commit): the current project can
      be saved as a reusable template — `apiSaveAsTemplate(name)` writes a `.gloopy` into a
      user templates dir (`<userAppData>/Gloopy/templates`, or `$GLOOPY_TEMPLATE_PATH`, the
      presets-dir pattern), preserving the open project's title. `apiListTemplates` now merges
      built-ins + those files (built-ins win a name clash) and `apiNewFromTemplate` seeds from
      either (a user template loads via `openAny` into a NEW untitled project). SaveAsTemplate
      RPC + Python `save_as_template`; the File menu gained "Save as Template..." (name prompt)
      and its "New from Template" submenu is now built from `apiListTemplates` so user
      templates appear alongside built-ins. smoke: save -> lists -> NewProject -> NewFromTemplate
      restores the tracks (snapshot+restore around the destructive New* calls); screenshot-
      validated (the menu shows a saved "My Song Starter" under the built-ins). Still to build:
      the browser sidebar.
    - `[x]` **Busy overlay / spinner landed** (commit, user request): BusyOverlay + runBackground
      (thread-pool + callAsync swap-in); SFZ + sampler loads run off-thread with a spinner so
      the UI no longer freezes. Long ops elsewhere (project open, plugin scan, audio import)
      can adopt runBackground next.
17. **Stronger piano-roll editing modes** — knife/split, strum, duplicate, transpose,
    quantize, humanize, velocity tools, ghost notes, scale highlight, step recording
    *(Idea #5; shares the note model with the step grid)*. **L**
    - `[~]` **Note ops landed** (`Source/NoteEdits.h` + ClipOps.cpp + PianoRoll, commit):
      shared `quantizeNotes`/`transposeNotes`/`humanizeNotes` used by BOTH the control
      API (`apiQuantizeClip`/`apiTransposeClip`/`apiHumanizeClip`, verifiable via
      GetClipNotes) and the PianoRoll UI (keyboard: Q/Shift+Q quantize 1/16·1/8, ↑/↓
      ±1 & Shift ±12 transpose, H humanize). RPCs + Python client. Verified headless
      (0.1→0.0, 60→72, jitter ≤±0.02). **UI needs visual eval.** (knife, strum, and legato
      tools have since landed as their own slices.) **Not yet:** step recording, velocity-tool
      drag, per-note selection ops.
    - `[x]` **Non-destructive per-clip transpose landed** (commit): a `Clip.transpose`
      (semitones, +/-48) applied at render time in `collectNotes` (clamped, same value on
      note-on/off so pairs match) — the stored notes are untouched, distinct from the
      destructive `apiTransposeClip`. So a clip can be re-pitched live (and A/B'd) without
      editing its notes; the arp path inherits it. `apiSetClipTranspose` + SetClipTranspose
      RPC + Python; serialised in the CLIP ValueTree + composition TOML (omitted when 0).
      **Desktop:** a "Transpose" submenu on the clip right-click menu (-12/-7/-5/-2/Reset/
      +2/+5/+7/+12, with interval labels). smoke proves +12 doubles a sine's zero-crossing
      rate (1.999), leaves the stored pitch at 69, and survives a composition round-trip;
      screenshot-validated (the submenu).
    - `[x]` **Non-destructive per-clip velocity scale landed** (commit): a `Clip.velocityScale`
      (0..2 multiplier) applied to each note's velocity at render time in `collectNotes` — the
      stored velocities are untouched (the pair to clip transpose; completes non-destructive
      "clip performance" controls). `apiSetClipVelocity` + SetClipVelocity RPC + Python;
      serialised in the CLIP ValueTree + composition TOML (omitted when 1.0). **Desktop:** a
      "Velocity" submenu on the clip right-click menu (25/50/75/100/125/150/200%). smoke proves
      0.5x exactly halves the render RMS (velocity->amplitude, 442239 vs 884477), 0x renders
      silent, and the stored note velocity stays 0.90 (via GetClipNotes); screenshot-validated.
    - `[x]` **Per-note probability (generative gate) landed** (commit): each `Note` gains a
      `probability` (0..1); `collectNotes` fires it only when a DETERMINISTIC per-note-
      per-repetition hash (`noteFires` in NoteScheduler.h — a fixed integer bit-mix of pitch +
      quantised note-start + quantised repetition-start) falls below the probability. So
      renders stay bit-reproducible while each looped repetition rolls independently — a
      generative sequencer staple, fitting the scriptable/generative north star. Serialised
      everywhere a note is (NOTE ValueTree, composition `.notes` 5th column, proto `Note.
      probability`, GetClipNotes, NotesJson). `apiSetClipProbability` (bulk, all notes) +
      SetClipProbability RPC + Python `set_clip_probability`; per-note via AddClip. **Desktop:**
      a "Probability" submenu on the clip menu (100/75/50/25/10%). smoke proves 1.0 = full,
      0.0 = silent, 0.5 = partial (rms between), and the 0.5 render is byte-identical across a
      composition round-trip (deterministic); screenshot-validated. Gotcha: proto3 omits 0.0,
      so an unset `Note.probability` (=0) is read as 1.0 in AddClip (a 0% note is meaningless).
    - `[x]` **Arp probability (generative gate) landed** (commit): the live arpeggiator gains a
      per-track `ArpSpec.probability` (0..1) — `expandArp` stamps it onto every generated step
      so the SAME deterministic `noteFires` gate in `collectNotes` drops steps reproducibly
      (each looped repetition rolls independently). `probability` on the SetTrackArp/GetTrackArp
      `ArpSpec` message + Python `set_track_arp(probability=)`; serialised on the track ValueTree
      (`arpProb`, omitted when 1.0). **Desktop:** a "Chance ▸ 100/75/50/25%" submenu on the ARP
      config menu. smoke proves 1.0 reproduces the default (unset) arp render byte-for-byte
      (proto3 unset=full), 0.5 drops steps (quieter, non-silent) and is byte-identical across a
      project round-trip; screenshot-validated (the expanded Chance submenu, 100% checked). Same
      proto3 gotcha as note probability: the SetTrackArp handler maps an unset (0.0) probability
      to 1.0 so existing clients aren't silenced.
    - `[x]` **Arp Gate menu landed** (commit): closed a desktop gap — the live arp's `gate` (each
      arp note's length as a fraction of the step, which `expandArp` already applied) was set
      only via the API; the ARP config menu passed the existing gate through unchanged. Added a
      "Gate ▸ 25% (staccato) / 50% / 75% / 100% (legato)" submenu, reflecting + setting the gate.
      Pure desktop wiring of the existing ArpSpec.gate / apiSetTrackArp param (no proto/API
      change). smoke proves a legato gate (1.0) renders fuller than staccato (0.25) — rms 937k vs
      411k on a sustaining synth — and the gate round-trips via GetTrackArp; screenshot-validated
      (the expanded Gate submenu, 50% checked). The arp's desktop controls are now complete
      (Rate/Octaves/Mode/Swing/Chance/Gate/Hold).
    - `[x]` **Scale highlighting landed + screenshot-validated** (commit): `PianoRoll::
      setScale(root,intervals)` builds a 12-pitch-class mask; `paint()` tints in-scale
      rows (chromatic ⇒ off); wired from `apiSetScale` + `refreshUiAfterLoad`. Verified
      by offscreen screenshot (Xvfb capture) — bumped tint 0.10→0.22 alpha for
      readability after the shot showed it too faint.
    - `[x]` **Strum landed** (commit a4e353e): shared strumNotes transform + apiStrumClip +
      StrumClip RPC + STRUM header button (Shift-click = up) + S/Shift+S keys. Smoke +
      screenshot validated.
    - `[x]` **Legato landed** (commit): shared `legatoNotes` transform (NoteEdits.h) stretches
      each note's length so it reaches the NEXT distinct onset, blended by amount (0 unchanged,
      1 = notes exactly touch); chords extend together (uses the next *distinct* start), the last
      onset group is unchanged, size/order preserved. `apiLegatoClip` + LegatoClip RPC
      (`LegatoRequest`; the handler maps an unset amount 0→1 like arp probability) + Python
      `legato_clip`; PianoRoll `legatoRollNotes` + a "LEGATO" header button (Shift-click = half)
      + L/Shift+L keys. `GloopyTests::NoteEdits` proves the length math (0/2/4 → 0..2/2..4/last
      unchanged, the amount blend, and chord-together); smoke proves LegatoClip via GetClipNotes;
      screenshot-validated (before/after in the piano roll: the LEGATO button stretches the notes
      to the next onset).
    - `[x]` **Velocity ramp (crescendo/decrescendo) landed** (commit): shared `rampVelocities`
      transform (NoteEdits.h) linearly interpolates each note's velocity from `fromVel` at the
      first onset to `toVel` at the last, by start-beat position (chords share a velocity; a
      single onset gets `toVel`); size/order-preserving. `apiRampClipVelocity` + RampClipVelocity
      RPC (`VelRampRequest`) + Python `ramp_clip_velocity`. **Desktop:** a "Velocity ramp ▸
      Crescendo / Decrescendo" submenu on the MIDI-clip right-click menu (crescendo 0.3→1.0,
      decrescendo 1.0→0.3). `GloopyTests::NoteEdits` proves the linear interpolation + chord/
      single-onset edges; smoke drives RampClipVelocity and checks GetClipNotes (0.3/0.65/1.0);
      screenshot-validated (the expanded submenu). Distinct from the non-destructive per-clip
      `velocityScale` (a uniform multiplier) — this writes a shaped dynamic into the notes.
    - `[x]` **Clip time-scale (double/half-time) landed** (commit): a pure `scaleNoteTimes`
      transform (NoteEdits.h) multiplies every note's start + length by a factor, plus a
      dedicated `apiScaleClipTime` that also scales the clip's `contentLenBeats` and `lengthBeats`
      so the loop window + arrangement slot follow (double-time 0.5 = twice as fast, half-time
      2 = twice as slow). ScaleClipTime RPC (`ScaleTimeRequest`; handler maps unset factor 0→0.5)
      + Python `scale_clip_time`. **Desktop:** a "Time ▸ Double-time (faster) / Half-time
      (slower)" submenu on the MIDI-clip right-click menu. `GloopyTests::NoteEdits` proves the
      scaling + inverse; smoke drives ScaleClipTime 0.5 and checks GetClipNotes (0/1/2, len 0.5)
      AND that the saved clip's `len`+`content` halved (6→3); screenshot-validated (the expanded
      submenu). A distinct rhythmic op (not a note-only transform — it resizes the clip).
    - `[x]` **MIDI echo landed** (commit): a shared `echoNotes` transform (NoteEdits.h) that
      APPENDS `repeats` decaying copies of every note — each `delayBeats` later, velocity ×
      `feedback` per step, copies fading below ~1% dropped; originals kept, pitch/length
      preserved. A generative note *multiplier* (grows the list), distinct from the shaping
      transforms. `apiEchoClip` (via GLOOPY_EDIT_CLIP_NOTES) + EchoClip RPC (`EchoRequest`;
      handler defaults unset repeats/feedback to 3 / 0.6) + Python `echo_clip`. **Desktop:** an
      "Echo ▸ 1/8 note ×3 / 1/16 note ×4" submenu on the MIDI-clip menu. `GloopyTests::NoteEdits`
      proves the decaying repeats + the fade-out drop + originals-kept; smoke drives EchoClip and
      checks GetClipNotes (0.5/1.0/1.5 @ 0.4/0.2/0.1); screenshot-validated (the expanded submenu).
    - `[x]` **Melodic inversion landed** (commit): a shared `invertNotes` transform (NoteEdits.h)
      mirrors every note's pitch around the earliest note's pitch (`newPitch = 2·pivot − pitch`,
      clamped 0..127) so intervals flip direction — a core compositional device; timing/length/
      velocity kept, size/order-preserving, self-inverse. `apiInvertClip` (via GLOOPY_EDIT_CLIP_
      NOTES) + InvertClip RPC (reuses `ClipRef`) + Python `invert_clip`. **Desktop:** an "Invert
      (mirror pitches)" item on the MIDI-clip menu. `GloopyTests::NoteEdits` proves the mirror +
      double-invert restore; smoke drives InvertClip and checks GetClipNotes (60/64/67 → 60/56/53);
      screenshot-validated (the menu item).
    - `[x]` **Ratchet / roll landed** (commit): a shared `ratchetNotes` transform (NoteEdits.h)
      subdivides every note into `subdivisions` equal SAME-pitch hits filling its span (a drum
      roll / stutter) — distinct from arpeggiate (which sequences a chord's *different* pitches).
      Size-changing; pitch/velocity kept. `apiRatchetClip` (via GLOOPY_EDIT_CLIP_NOTES) +
      RatchetClip RPC (`RatchetRequest`; handler defaults unset subdivisions to x2) + Python
      `ratchet_clip`. **Desktop:** a "Ratchet ▸ x2/x3/x4" submenu on the MIDI-clip menu.
      `GloopyTests::NoteEdits` proves the equal subdivision + chord case; smoke drives RatchetClip
      x4 and checks GetClipNotes (a 1-beat note → 0/0.25/0.5/0.75 len 0.25); screenshot-validated
      (the expanded submenu).
    - `[x]` **Harmonize landed** (commit): a shared `harmonizeNotes` transform (NoteEdits.h) adds a
      parallel voice `semitones` from every note (keeping the originals) — a monophonic line becomes
      parallel harmony. Distinct from transpose (which *moves* every note) and invert (which
      mirrors): harmonize *thickens*, growing the list; the added voice shares the note's start/len/
      velocity and is **dropped** (not folded) if it falls off 0..127. `apiHarmonizeClip` (via
      GLOOPY_EDIT_CLIP_NOTES) + HarmonizeClip RPC (`HarmonizeRequest`) + Python `harmonize_clip`.
      **Desktop:** a "Harmonize ▸ Minor 3rd (+3) / Major 3rd (+4) / Perfect 5th (+7) / Octave (+12) /
      Octave down (-12)" submenu on the MIDI-clip menu. `GloopyTests::NoteEdits` proves the +7 voice
      (60/64 → 60/64/67/71, timing/vel kept) and the off-keyboard drop; smoke drives HarmonizeClip +7
      and checks GetClipNotes; screenshot-validated (the expanded submenu).
    - `[x]` **Swing / groove (destructive per-clip) landed** (commit): a shared `swingNotes`
      transform (NoteEdits.h) delays every OFF-beat note (one sitting on an odd `grid` line — the
      &-of-the-beat) by `amount·grid`, baking a shuffle into the clip; on-beats stay, length/pitch/
      velocity kept. Distinct from the existing *live* transport swing (`SetSwing`, global +
      non-baked) — this writes the groove into the notes so a clip carries its own feel and can
      export it. `apiSwingClip` (via GLOOPY_EDIT_CLIP_NOTES) + SwingClip RPC (`SwingClipRequest`;
      handler defaults unset grid to 1/8) + Python `swing_clip`. **Desktop:** a "Swing ▸ 1/8
      light/medium/heavy · 1/16 light/medium/heavy" submenu on the MIDI-clip menu (grid 0.5/0.25,
      amount 0.2/0.33/0.5). `GloopyTests::NoteEdits` proves off-beats shift while on-beats/length
      hold (0/0.5/1/1.5 @ 1/8 0.33 → 0/0.665/1/1.665) and amount 0 = straight; smoke drives
      SwingClip and checks GetClipNotes; screenshot-validated (the expanded submenu).
    - `[x]` **Chordify (named chords) landed** (commit): a shared `chordifyNotes` transform
      (NoteEdits.h) turns every note into a full chord by adding a voice at each interval of a
      named chord (root kept as the played note; off-keyboard voices dropped). Generalises
      harmonize (single interval) — `apiChordifyClip(chordType)` maps 0 major {+4,+7} / 1 minor
      {+3,+7} / 2 dom7 {+4,+7,+10} / 3 dim {+3,+6} / 4 sus4 {+5,+7} to interval sets. ChordifyClip
      RPC (`ChordifyRequest`; 0=major is the proto3 default) + Python `chordify_clip`. **Desktop:**
      a "Chord ▸ Major / Minor / Dominant 7th / Diminished / Sus4" submenu on the MIDI-clip menu.
      `GloopyTests::NoteEdits` proves the major triad (60 → 60/64/67, timing/vel kept), the dom7
      voice count, and the off-top drop (124+{3,7} → 124/127); smoke drives ChordifyClip major and
      checks GetClipNotes; screenshot-validated (the expanded submenu).
    - `[x]` **Arpeggiate (destructive one-shot) landed** (commit c90da23): shared arpeggiateNotes
      + apiArpeggiateClip + ArpeggiateClip RPC + ARP header button (Up/Down/Up-Down menu).
      Rewrites a chord clip into a note sequence in place. Smoke + screenshot.
    - `[x]` **LIVE arpeggiator landed** (commits eb28823 + d47b188 + caeec9c + 907ed3d, user-
      requested): non-destructive per-track MIDI-effect arp. `expandArp` (NoteEdits.h) STEP-
      SAMPLES the held notes at each rate step (chord arpeggiates, single note repeats, octaves
      cycle, overlaps included — matches Ableton/Logic per research). Per-track ArpSpec
      (enabled/rate/octaves/gate/mode) played via transient Clip::arpNotes swapped under the
      lock (audio thread stays arp-logic-free); recomputed on edit/set/load; persisted.
      SetTrackArp/GetTrackArp RPCs + Python. UI: per-track ARP button + config menu. Verified:
      unit tests + smoke render-diff + screenshot. SWING + HOLD added (commit 1026514):
      swing delays every other step; hold latches the last chord across rests / fills the
      clip. Both in ArpSpec + API + menu + persisted + unit-tested. Follow-ups possible:
      more note-orders (converge/diverge/as-played), triplet/dotted rates.
    - `[x]` **Snap-to-scale-while-drawing landed** (commit 9be97f6): SCALE header toggle snaps
      drawn/moved pitches to the project scale (roll scaleMask). Wave 6 Tier-1 batch COMPLETE.
    - `[x]` **Velocity ramp tool landed** (commit 55d7a19): shift-drag the velocity strip draws
      a linear crescendo/decrescendo across the spanned notes; plain drag still scrubs one.
      Screenshot + functional validated.
    - `[x]` **Knife / split-note tool landed** (commit): a shared `splitNotesAtBeat(notes,
      beat)` transform (`Source/NoteEdits.h`) cuts every note crossing a clip-relative beat
      into two abutting notes (pitch/velocity preserved; notes starting AT the cut or not
      crossing it are untouched). Drives both `apiSplitNotesAtBeat` (SplitNotesAtBeat RPC +
      Python, GetClipNotes-verifiable) and the PianoRoll KNIFE mode — a KNIFE header toggle;
      when on, a click cuts the notes at the clicked beat. `NoteEditTests` covers the split
      (spanning note -> halves, boundary/after notes untouched); smoke proves a 2-beat note
      split at beat 1 -> 60@0/60@1 with 64@1 left intact; screenshot-validated (KNIFE active +
      notes cut on click). **Not yet:** step recording.
    - `[x]` **Vertical zoom + scroll landed** (commit 5daac0e, user request): dynamic pitch
      window (viewRows/viewTop over A0..C8); wheel scrolls, Ctrl/Cmd+wheel zooms (cursor-
      anchored); default ~2.5x more zoomed + auto-frames the clip's notes on load.
    - `[x]` **Region selection landed** (commit db8e145, user request): shift-drag marquee
      selects notes (gold highlight); shift-click toggles; group-move / Q/H/S/arrows /
      Delete act on the selection. Screenshot + functional validated (group move).
    - `[x]` **Note auditioning landed** (commit 1d3c84c): the piano roll plays notes
      through the edited track's instrument as you interact — click the key gutter (brush
      down the keys to scrub pitches), draw/move a note (chords sound all voices),
      re-triggering as pitch changes. AUDITION header toggle (default on). Routed via
      `apiAuditionNote` → the track's live-MIDI collector (same lock-free path as MIDI-in
      / OSC), so nothing added to the render thread. Validated: toggle renders + audio
      callback live (GetDiagnostics) + click path fires. Like FL/Ableton/Cubase.
    - `[x]` **Ghost notes landed + screenshot-validated** (commit): other instrument
      tracks' notes overlapping the edited clip's window are gathered in
      `loadSelectedClipIntoEditor` (mapped to clip-relative beats) and drawn dim behind
      the editable notes (`PianoRoll::setGhostNotes` + paint). Verified by screenshot
      (lead melody bright, harmony/kit notes dim gray behind).
    - **Screenshot validation now works** (see the `fix(startup)` commit + the offscreen
      Xvfb recipe): Xvfb → launch → drive via gRPC → `xdotool` (select clip / click
      PIANO tab) → `ffmpeg x11grab :N` → read PNG. NB: never `pkill -f` a pattern that
      matches the running shell (use tracked PID or `pkill -x`); the live desktop is
      Wayland so X11 grab there is black — capture on Xvfb.
18. **Richer sampler controls + cached waveform thumbnails** — start/end/loop/reverse,
    root note, choke group, fades, interpolation; a multi-resolution peak cache keyed
    by path+mtime+size, reused across clips/sampler/browser/exports *(Idea #6/#7)*. **M/L**
    - `[~]` **Playback window landed** (`Source/Sampler.h`, commit): the one-shot Sampler
      gained a playback window — `start`/`end` as fractions [0,1] of the sample length and
      a `reverse` flag (voice `pos` starts at the window edge, `rate` is signed, and the
      window bounds are kept in-range so the linear interpolator never reads off the buffer).
      A live `apiSetSamplerControls`/`apiGetSamplerControls` (window + reverse + root note)
      set under engineLock; SetSamplerControls/GetSamplerControls RPCs + Python
      set/get_sampler_controls. Serialised in both the ValueTree SAMPLE child and the
      composition sampler TOML (start/end/reverse). **Desktop UI:** right-click a Sampler
      track header -> "Sampler" prompt (Start/End/Root/Direction), screenshot- AND
      functionally-validated (GUI Apply -> GetSamplerControls shows start=0.5, reverse=true).
      smoke proves reverse + start-trim move an asymmetric sample's tone to the front and the
      controls round-trip.
    - `[~]` **Per-voice fades landed** (commit): `fadeIn`/`fadeOut` (seconds) on the Sampler —
      a linear amplitude ramp from the note-on and a linear ramp approaching the end of the
      voice's playback window (each voice tracks `age`/`life` in output samples). De-clicks a
      sample trimmed mid-waveform by the playback window. Extended the SAME plumbing as the
      window slice: `apiSetSamplerControls`/`apiGetSamplerControls` + the SamplerControls
      proto messages + Python + the ValueTree/composition serialisation + the "Sampler"
      header prompt (Fade in/out fields). smoke proves a fade-in ramps a steady tone's start
      (early peak far below the post-fade level, while the no-fade render stays flat) and
      `fade_in` round-trips; screenshot-validated. Gotcha logged: the render WAV is stereo
      interleaved (read one channel), and grpcurl emits camelCase JSON keys (`fadeIn`).
    - `[~]` **Loop mode landed** (commit): a Sampler voice can loop its playback window
      (sustained) until a note-off releases it, instead of playing once. A one-shot still
      ignores note-offs (drums ring out) — loop mode is the only path that tracks the voice's
      note number and handles note-off; on release it uses `fadeOut` as the release time (an
      immediate stop when 0). The window-end fade is suppressed while looping. Extended the
      SAME plumbing: `loop` on apiSet/GetSamplerControls + the SamplerControls proto + Python +
      ValueTree/composition serialisation + a "Mode" (One-shot/Loop) combo on the header prompt.
      smoke: a short (~45 ms) sample under a 1-beat note is silent mid-note as a one-shot but
      SUSTAINS when looped, and goes silent again PAST the note-off (proving note-off releases
      the loop); the loop flag round-trips through a composition. Screenshot- and functionally-
      validated (GUI Mode=Loop -> GetSamplerControls loop=true).
    - `[x]` **Sampler mono / choke landed** (commit): a `Sampler.mono` flag — a new note-on cuts
      every currently-ringing voice (`startVoice` deactivates all voices first), so overlapping
      hits don't stack (classic hi-hat choke / mono 808). Extended the SAME plumbing: `mono` on
      apiSet/GetSamplerControls (+ default arg) + the SamplerControls proto (both messages) +
      Python + ValueTree (`smono`, omitted when off) / composition (`groups`-style `mono` field)
      serialisation + a "Voices" (Poly / Mono (choke)) combo on the header prompt. smoke: a long
      2 s tone under two hits at DIFFERENT pitches a beat apart — polyphonic both ring in the
      overlap window (RMS 769k) but mono chokes the first (492k); mono round-trips via
      GetSamplerControls. Screenshot-validated (the Voices combo expanded to Poly / Mono (choke)).
      **Not yet:** cross-track choke *groups*, root-note keyboard-mapped multisamples, loop
      crossfade, interpolation quality, waveform thumbnails/peak cache.
19. **Controller rack / MIDI-learn / parameter linking + MIDI device maps** — a
    source→target mapping view; MIDI-learn for any `ParamModel` id; OSC/API sources as
    mappable controllers; per-mapping scaling/inversion/smoothing/range/bypass;
    data-driven device maps for common controllers; persisted in composition files
    *(Idea #4 + Ardour #6; rides on Wave 1 #1)*. **L**
    - `[~]` **Controller mapping + MIDI-learn backend landed** (`Source/Controllers.cpp`,
      commit): `CtrlMap{source,target,lo,hi}` binds a source (`cc:<n>` MIDI CC,
      `osc:<name>`, or any string) to a ParamModel id, scaling 0..1 → [lo,hi].
      `apiSetController` (the single feed) is called by the MIDI CC callback
      (`handleIncomingMidiMessage`) and the SetController RPC; `apiMidiLearn(target)`
      arms capture of the next controller (auto-ranged to the param's min/max). Uses
      the lock-held `applyParamValue` writer. Add/List/Remove/Set/Learn RPCs + Python.
      Serialised (CONTROLLERS + composition `controllers.toml`). Verified headless:
      cc:1→cutoff scales exactly, learn captures cc:7→reso, round-trips; smoke asserts
      it. **UI FLAGGED — no mapping-view / learn-button UI yet** (the mapping is usable
      now via hardware CC knobs + API, but the visual rack is a follow-up).
    - `[x]` **Inversion + per-map bypass landed** (commit): *inversion* was already
      implicit — `lo + v01*(hi-lo)` reverses when `lo>hi`, now verified/documented. Added
      `CtrlMap.bypass` (disable a map without removing it): `apiSetControllerBypass`
      (keyed by source+target) + `SetControllerBypass` RPC + Python; `apiSetController`
      skips bypassed maps; serialised in CONTROLLERS + `controllers.toml`; ListController
      Maps returns the flag. smoke proves an inverted map (cc value 1 → low cutoff) and
      that bypass freezes the param while un-bypass re-applies.
    - `[x]` **Desktop see/remove of a mapping landed** (commit): the mixer param right-click
      menu (the same one with MIDI Learn / LFO / Automate) now shows the CC/OSC source bound
      to that param in its header (`Ins 1 volume (cc:20)`) and offers "Remove mapping (cc:20)"
      when mapped — so a MIDI-learned binding can be *seen and cleared* from the desktop, not
      only via the API. Pure UI wiring on the tested controller backend (`onControllerSourceFor`
      / `onRemoveControllerMap` iterate `apiListControllerMaps` by target) — screenshot-
      validated.
    - `[x]` **Mapping-rack VIEW landed** (commit): a "Maps" toolbar button opens a Mappings
      window (`Source/MappingsView.h`) listing every controller map AND LFO route with a
      per-row Remove (see the Wave-6 UI-parity note above). **Not yet:** in-rack editing of a
      map's range/bypass/smoothing, device-map files, OSC-lane wiring.
20. **Product-surface tier** — in-app markdown **project notes** under `notes/`
    (Idea #10); a static-file **localhost web control surface** for transport/mixer/
    markers/live notes, doubling as an API test client (Ardour #7); ~~an MCP tool
    surface~~ (**promoted to its own epic — Wave 11 #33**); a **script browser** exposing
    composition-local + built-in scripts as
    installable actions that run against the gRPC API — not an embedded VM (Ardour #4).
    **L**, each independent.
    - `[~]` **Project notes landed** (`Source/Notes.cpp`, commit): free-form markdown
      (`projectNotes`) with `SetProjectNotes`/`GetProjectNotes` RPCs + Python client;
      serialised on the root ValueTree + composition `notes.md` (readable markdown,
      manifest-referenced). UI: File → "Project Notes..." opens a `TextEditor` window
      synced to the model. NewProject clears it. Verified headless: set → save →
      NewProject clears → reload restores. **UI needs visual eval.** **Not yet:** web
      surface + script browser (each still independent L slices; the MCP surface is now
      Wave 11 #33); notes/ subdir for multiple docs (lyrics/credits split).

### Wave 7 — Engine architecture & correctness (lessons from Tracktion Engine) ✦

Studied `~/git/tracktion_engine` (the production JUCE DAW engine, ~376k LOC across
`tracktion_core` / `tracktion_graph` / `tracktion_engine`). Every item below is
**borrow-the-idea only** — Tracktion Engine is GPL/dual-licensed; do NOT copy its code
or assets, and keep its name out of shipped code/commit messages per the guardrail
(this internal roadmap cites it the same way it cites Ardour). File pointers are
prior-art references to *read*, not to lift.

21. **Strong time types — `BeatPosition` / `TimePosition` / `*Duration`.** ✦ **M**
    Replace bare `double beat` / `int samples` passed everywhere with distinct wrapper
    types (position vs duration are different types; only sensible combinations compile:
    position ± duration → position, position − position → duration). Prior art:
    `tracktion_core/utilities/tracktion_Time.h:31-338`. *Done when:* clip/loop/marker
    positions and `beatToSamples`/`samplesToBeats` are expressed in the new types; a
    `GloopyTests` case proves the illegal combinations don't compile (or are rejected).
    Low risk, no audio-thread danger, big safety win — **near-term (priority #2).**

22. **Undo / redo — fine-grained (ValueTree + `UndoManager`).** ✦ **L** — *deferred/optional.*
    NOTE: snapshot-based undo **already ships** and works (see the reprioritization banner;
    commit c3eedf0 surfaced it in the menu + added a test). This item is the *optional*
    Tracktion-style refinement — finer granularity + lower memory than full-project
    snapshots. Only worth it if snapshot undo proves too coarse/heavy. Route *all* model
    mutations through the
    ValueTree with an `UndoManager` (Tracktion binds each property with
    `CachedValue<T>.referTo(state, id, um)` and manages Track/Clip C++ objects with a
    `ValueTreeObjectList` listener — child added → create wrapper, removed → destroy).
    Undo then "just works" from the tree deltas. Prior art:
    `tracktion_ValueTreeUtilities.h:89-203` (ValueTreeObjectList), `tracktion_Clip.cpp:77-90`
    (CachedValue), `tracktion_Edit.h` (single `state` tree = source of truth). *Done when:*
    Ctrl-Z / Edit-menu Undo+Redo reverse track/clip/param edits; multi-step undo survives;
    a headless test mutates → undoes → asserts the tree is byte-identical to before.
    **Desktop control required** (Edit menu + Ctrl-Z/Ctrl-Shift-Z). Staged: (a) adopt an
    `UndoManager` + route one subsystem (e.g. clip edits) through it; (b) convert the rest;
    (c) `ValueTreeObjectList` to retire manual `buildTrackFromTree` rebuilds.

23. **Stable-ID remapping — generalize `EditItemID`.** **M**
    Tracktion gives every object a stable id and a built-in `remapIDs(old→new)` for
    copy/paste/duplicate. Gloopy's duplicate-track slice hand-rolled exactly this
    (drop `tid`, reassign). Generalize into one id-remap pass over a subtree so
    duplicate/paste of *any* object (clips, lanes, sends) is reference-safe. Prior art:
    `tracktion_EditItem.h:17-79`. *Done when:* duplicating a track/clip that references
    other ids (sends, sidechains, automation targets) rewrites those refs correctly;
    round-trips in the composition.

24. **`AudioBufferPool` — no allocation on the audio thread.** ✦ **M** — **near-term (#3).**
    Pre-allocate a lock-free FIFO of buffers; nodes/strips `allocate()`/`release()`
    instead of touching `new` in `getNextAudioBlock`. Prior art:
    `tracktion_graph/.../tracktion_AudioBufferPool.h`. *Done when:* a churn test proves
    zero heap allocations on the audio thread during playback (hook the allocator / count).
    Cheap, self-contained, and a prerequisite for any graph rework.

25. **Off-thread graph build + atomic swap.** ✦ **L** — *deferred* (do #24 first).
    Build the new mixing graph off the audio thread and swap it in lock-free
    (double-buffered holder; audio thread `try_lock`s, edit thread pushes non-RT). Also:
    monotonic **reference sample-range** through the render (looping/scrubbing becomes a
    block-split, not per-node logic), per-node **PDC** with FIFO reuse across rebuilds to
    avoid clicks. Prior art: `tracktion_LockFreeMultiThreadedNodePlayer.cpp:276`,
    `tracktion_LockFreeObject.h`, `tracktion_LatencyNode.h`, `tracktion_PlayHead.h`.
    Only worth it once Gloopy has dynamic routing (aux sends/returns, #7). **Touches the
    audio thread — principle-4 territory; probe under churn before integrating.**

26. **Multicore audio rendering — work-stealing node scheduler.** ✦ **XL** — *deferred,
    gated on profiling.* Today Gloopy sums all tracks **serially on the one audio thread**
    (`getNextAudioBlock`, `MainComponent.cpp`); Tracktion renders the graph across
    `NumCPUsForAudio − 1` workers **plus** the audio thread, via atomic per-node
    input-counters draining a lock-free ready-queue (independent tracks/plugin chains run
    on different cores). Prior art: `tracktion_LockFreeMultiThreadedNodePlayer.cpp` (the
    `process()` drain loop + `updateProcessQueueForNode`), `tracktion_NodePlayerThreadPools.h`
    (6 wake strategies to avoid CVs on the audio thread). **Do NOT start until profiling
    shows a real single-core ceiling on a heavy multi-track session** — parallelism only
    helps *independent* work (Amdahl: a deep serial plugin chain can't be split), and the
    real cost is the entire lock-free-graph architecture (#24 + #25), not a thread pool.
    When the time comes, port Tracktion's shape rather than invent one.

27. **In-process deterministic render test harness + sanitizer CI.** ✦ **M**
    Tracktion tests audio *offline and deterministically*: fixed-block in-process render
    to a buffer + peak/RMS assertions (no external tools, no running app), tests co-located
    and gated by compile flags, and CI running **ASan/TSan** presets. Prior art:
    `testing/tracktion_EnginePlayer.h:26-124`, `model/export/tracktion_Renderer.h`,
    `CMakePresets.json`. *Done when:* a `GloopyTests`-style harness renders a fixed edit to
    a buffer and asserts peak/LUFS in-process (complementing `smoke.sh`'s WAV-shell-out);
    and CMake gains `asan`/`tsan` presets wired into a CI matrix. High value precisely
    because we're about to touch audio-thread code (#24/#25) — sanitizers catch the bugs
    that only bite under load.

28. **Surge XT as the embedded default synth + curated presets.** ✦ **L (multi-session)**
    *User-requested (2026-07-26).* Embed the Surge XT synth **engine** into Gloopy's build
    (mirroring the vendored sfizz `Generator`) and bundle a curated ~150–300 first-party
    patch set, so new synth tracks default to Surge and Gloopy sounds great out of the box,
    self-contained. **Full plan + licensing analysis + sliced sequence in
    `docs/surge-embed.md`.** Key facts: Surge is **GPL-3.0** (compatible to combine with
    Gloopy's AGPL-3.0 per GPLv3/AGPLv3 §13 — record in THIRD-PARTY-LICENSES.md; the shipped
    binary becomes a combined GPL/AGPL work); synth core is the `surge::surge-common` CMake
    lib driven headless via `createSurge()` + `HeadlessPluginLayerProxy` (already known-good
    — Surge is installed + hosted here); vendor by **submodule** under `third_party/surge`
    (NOT a 1.2 GB copy) + a curated data subset (~few MB). Slices: (1) **isolation probe —
    ✅ DONE (commit)**: built `surge-common` headless (no JUCE), linked `docs/surge/probe.cpp`,
    scanned **3,559 patches** + rendered **non-silent** audio (RMS 0.070); proven build recipe
    recorded in `docs/surge-embed.md` (submodule set, cmake skip-flags incl. `SURGE_SKIP_WERROR`,
    consumer `-std=c++20 -fno-char8_t`, PUBLIC include/BLOCK_SIZE contract). (2) **CMake link —
    ✅ DONE (commit)**: `GLOOPY_WITH_SURGE` option (default ON) + `GLOOPY_SURGE_DIR`;
    `add_subdirectory` builds surge-common (92 objs) inside Gloopy's build + links it (smoke
    green; lean OFF-path clean); fixed the embedding gotcha (surge-common's `${CMAKE_SOURCE_DIR}`
    r8brain ref mis-resolves to Gloopy → sed shim to `${SURGE_SOURCE_DIR}`). **2b — ✅ DONE (commit)**:
    added the `third_party/surge` git submodule (github URL, pinned `9e73f42c`, via `--reference` so
    Gloopy's .git stays tiny; `ignore = dirty`) + `scripts/init-surge.sh` that inits ONLY the sub-libs
    surge-common needs (JUCE skipped) — so the committed default `GLOOPY_SURGE_DIR=third_party/surge`
    builds. **Surge slice #28 is now COMPLETE** (all of 1–6). **Follow-up (#28b):** the embedded core has no UI. **User pivot (2026-07-26): build
    the Surge XT *plugin* from source, ship it, host it for the real editor.** First verifiable
    step landed (commit): the **"+ Synth → Surge XT (full editor)"** menu option hosts an
    installed/bundled Surge XT plugin (`apiAddPluginTrack`, prefers LV2) → the real Surge UI via
    the existing plugin-editor path; embedded default + Presets untouched. Remaining (CI-gated —
    can't build locally, no libGL/sudo): build the LV2 from the submodule
    (`scripts/build-surge-plugin.sh`), bundle it + add its dir to the plugin scan path (zero
    install), and (open) Presets → load `.fxp` into the hosted plugin (the generic host API can't
    load an arbitrary .fxp by path — needs a Surge-plugin patch-load path). Open with the user:
    retire headless vs keep as fallback; bundled-vs-installed default. See docs DIRECTION CHANGE. (3)
    **`SurgeGenerator` — ✅ DONE (commit)**: a Surge track renders non-silent audio through the
    mix (smoke peak ~0.33). Was crashing in `SurgeStorage` ctor — root cause was an **ODR clash**:
    vendored sfizz ships its own *older* `Tunings::` tuning-library (strong global symbol) that
    the linker preferred over surge-common's *newer inline* one, so surge called sfizz's
    incompatible-layout impl → SIGSEGV. Fixed by renaming sfizz's private copy `namespace Tunings`
    → `TuningsSfz` (3 files). Full detail in docs/surge-embed.md. (5a) **default-synth desktop
    control — ✅ DONE (commit)**: the toolbar **"+ Synth" now offers "Surge XT (default)" / "Basic
    synth"** (a menu when GLOOPY_WITH_SURGE, else one-click built-in) — Surge is the default voice
    while the built-in step-synth stays one click away; no toolbar overflow (rejected a separate
    "+ Surge" button that pushed Metro off). Screenshot-validated end-to-end (menu → Surge XT → a
    "Surge" instrument track). Built off the message thread behind the busy overlay (patch scan).
    (5b) **Presets browser tab — ✅ DONE (commit)**: a 5th browser tab "Presets" lists the **639
    first-party factory patches** (category-prefixed "Basses/Bass 1", sorted; `listSurgePatches`
    scans `<surge-data>/patches_factory`) — clicking one adds a Surge track loaded with that .fxp
    (via a shared `addSurgeTrackAsync` off-thread helper). Screenshot-validated (Presets tab lists
    patches; clicking "Basses/Bass 1" → a "Bass 1" instrument track). Only present when
    GLOOPY_WITH_SURGE + a data dir. (4) **factory data bundle — ✅ DONE (commit)**: per the user's
    choice, ALL 639 first-party patches + first-party wavetables (~31 MB) bundled into
    `third_party/surge-data/`; `dataDir()` resolves env → `GLOOPY_ASSETS_DIR/surge-data` → exe-adjacent
    (+ install rule); Presets tab populates + renders WITHOUT `$GLOOPY_SURGE_DATA` (screenshot + smoke).
    (5) default-synth wiring + preset browser (the
    desktop control); (6) **license/docs — ✅ DONE (commit)**: THIRD-PARTY-LICENSES.md gained an
    "Embedded synth (Surge XT) — GPL-3.0" section (combined GPL/AGPL work; GLOOPY_WITH_SURGE=OFF
    for a Surge-free build; first-party factory content only) + documented the sfizz Tunings→
    TuningsSfz rename as a local modification; README License note added. Curate ONLY from
    `patches_factory`/`wavetables` (skip
    `*_3rdparty` without per-pack review).

### Wave 8 — Session view (clip-launch grid) epic ✦

An **Ableton-style Session view alongside the linear Arrangement view, Tab to switch** —
columns = tracks, rows = scenes, each cell a launchable looping clip. Gloopy is well-placed:
`Clip` already loops and owns its content, so a session clip is just a `Clip` in a scene slot.
Locked scope (with the user): the **simplified color-bar grid** (not the dense icon version);
**Tab cycles Arrange → Session → Mixer**; **per-track override playback** (launching a session
clip overrides that track's arrangement playback while other tracks keep playing; "Back to
Arrangement" resumes all). Adopted Ableton defaults: global launch quant (1 bar), one playing
clip per track, global scene rows, launch-empty-slot-to-record. Session launching is a **live**
feature — NOT on the offline-render path, so determinism is unaffected (renders still come from
the arrangement). Prior art (shape only, no copy): Ardour Trigger/Cue + Tracktion `LaunchHandle`;
render-wiring design in `docs/session-view.md`.

29. **Session view — model → launch engine → UI+Tab → recording.** ✦ **L (multi-session)**
    - `[x]` **Slice 1 — model + persistence:** `SessionModel.h` (`Track::sessionSlots`,
      null = empty; a global `scenes` list); round-trips through `.gloopy` + composition TOML.
    - `[x]` **Slice 2 — launch engine:** `SessionLauncher.h` — sample-accurate clip/scene launch
      (block-split, pending → fires at the next launch-quant boundary), per-track exclusivity,
      scene launch = the whole row, stop-clip / stop-all, per-track arrangement override.
    - `[x]` **Slice 3 — Session UI + Tab:** `SessionView.h` grid (slots, launch cues, pulsing
      "queued" cue), Tab cycles Arrange/Session/Mixer, scene + **master column pinned LEFT as a
      frozen pane** (deliberate — not Ableton's right side), per-track group/return columns, a
      bottom **Device View** for the selected track's chain.
    - `[x]` **Slice 4 — session recording:** arm + launch an empty slot records live MIDI into
      it; **session → arrangement** capture (record a session take, copy a clip to the timeline).
    - `[ ]` **Slice 5 — control API:** OSC + gRPC launch clip/scene, stop, query session state
      (the one place this epic is still API-*un*reachable — do this next per principle 1).
    - `[ ]` **Slice 6 — polish:** clip colours in the grid, follow-actions, capture, a
      launch-quantize menu, launch-mode indicators.

### Wave 9 — Git as project management (composition-as-repo, realized) ✦

The north star is "the DAW you can drive from a script **and store in git**" — this epic
delivers the second half. Gloopy's composition-as-repo format is already a first-class git
citizen (readable TOML / `.notes` / `.points`, content-addressed dirty writes → minimal
diffs), so wrapping it in git makes **version control a native DAW workflow**: novel in a DAW,
familiar from every IDE. The bar (user direction, 2026-07-28) is an **IDE-grade source-control
surface** — the affordances you rely on in VS Code / JetBrains / GitKraken (Source Control panel,
commit graph, branches, tags, **checkout any version**, diff, stash, push/pull) — brought into a
DAW and framed musically: **branches = alternate arrangements / remixes, tags = milestone mixes
(demo / master / album-cut), checkout = load-and-audition an older cut**. It stays **git-the-tool
underneath** (we shell out — see the design fork below): a rich UI over real git, not a
reimplementation.

**✦ Design fork — shell out to the system `git`; do NOT vendor libgit2.** "Gloopy should make
sure git is available" = detect the `git` binary and reuse the user's existing identity, SSH
keys, and credential helpers, so push/pull "just work" with their setup. Shelling out via
`juce::ChildProcess` off the message thread means no new dependency, no auth reimplementation,
and no AGPL/GPL linking entanglement (a process boundary, not a link — fits principle 5's
"don't embed a second VM": reuse the platform tool). A `Source/Git.cpp` `runGit(dir, args) ->
{code, out, err}` helper is the whole substrate.

**Where git state lives:** in `.git`, *out of band* — NOT in the composition format. So this
epic adds **no new composition serialisation** (principle 2 is satisfied trivially: git wraps
the existing text format). Git targets the **folder composition** (the diff-friendly one);
a single-file `.gloopy` can be tracked too, but the win is the folder repo. A fresh untitled
project has no dir until first save, so "New Git Project" establishes dir → `git init` → save
up front; existing folder projects get "Enable Git" (init in place).

**Realtime + safety:** every git op is message-thread / off-thread `ChildProcess` — NEVER the
audio thread (principle 4). Network ops (push / pull / clone) run behind `runBackground` + the
busy overlay. **Push is outward-facing** — always an explicit user action, never automatic;
commit messages are user-authored; auto-commit-on-save is opt-in only.

30. **Git project management — IDE-grade source control for compositions.** ✦ **XL (multi-session)**
    *(Realizes the north star. Every slice is API-reachable + desktop-UI + headless-proven against a
    temp repo. Study the best IDE git UX for shape — VS Code Source Control, JetBrains Git, GitKraken
    — and adapt; the DAW lens makes each feature musical, not just plumbing.)*

    **Target surface (the IDE features we're matching), by group:**
    - *Repo & commits:* availability check, status, init / enable, stage (all + per-file; hunk/line
      staging a stretch), commit (message editor, changed-files tree, per-file diff, amend, sign-off).
    - *History:* commit **graph** / log view, per-commit diff, filter / search, file history + blame
      (stretch).
    - *Structure:* **branches** (create / checkout / rename / delete / new-from-here / merge),
      **tags** (lightweight + annotated: create / checkout / delete / list), **checkout any version**
      (branch / tag / commit → load the project at it).
    - *Working tree:* **diff viewer** (working tree + between any two revisions), discard, **stash**
      (shelve / pop), revert a commit, reset (guarded).
    - *Collaboration:* remotes, fetch / pull / push / sync, **merge-conflict resolution** over the
      composition text format.
    - *Config:* identity override, auto-stage / auto-commit toggles (opt-in), Git LFS for audio
      sidecars, commit templates.

    Sliced sequence (one green commit each; build the substrate first, then breadth):
    - `[x]` **1 — availability + status LANDED** (`Source/Git.cpp`, commit): `runGit()` substrate
      shells out to the system git via `juce::ChildProcess` (off the audio thread; git not yet
      used elsewhere). `apiGitAvailable(version)` (`git --version`) + `apiGitStatus(dirOverride)`
      → {available, is_repo, detached, branch (or "(<short-hash>)" detached), ahead/behind, dir,
      changes[xy,path]} parsed from `status --porcelain=v1 --branch` (+ `branch --show-current`).
      Resolves the open project's folder (gloopy.toml parent / .gloopy parent) via
      callOnMessageThread, then runs git on the caller thread so gRPC calls don't stall the message
      thread. GitAvailable/GitStatus RPCs (`GitVersion`/`GitDir`/`GitState`) + Python
      `git_available`/`git_status`. **Desktop:** File → "Source Control..." opens a read-only status
      window (branch + dirty file list; the seed of the full panel). smoke.sh: GitAvailable, a
      scratch repo clean→dirty (untracked `??`), and a non-repo dir reported as not-a-repo (all
      headless, git-absent-skipped). Manual how-to page + `mkdocs --strict` green.
      **Follow-up:** the panel grows a Refresh button + live dirty-count indicator in later slices.
    - `[x]` **2 — init / enable git LANDED** (`Source/Git.cpp`, commit): `apiGitInit(dir)` creates
      the folder if needed and runs `git init` (idempotent, so it doubles as Enable-Git); returns a
      `GitResult{ok,error}` (runGit gained an `alsoStderr` flag so failures surface). The `.gitignore`
      is already owned by the composition writer (`kGitignore`: `.gloopy-cache/`, `exports/`,
      `assets/recordings/raw/`, `*.wav.tmp`, `plugins/scans/`), so init doesn't write one. GitInit RPC
      (GitDir→Ack) + Python `git_init`. **Desktop:** File → "New Git Project..." (dir chooser →
      SaveComposition + apiGitInit off-thread via runBackground, then retargets `currentProjectFile`
      to the new gloopy.toml + opens Source Control) and "Enable Git" (enabled only for an open
      composition folder → apiGitInit its dir). smoke: SaveComposition into a fresh dir → GitInit →
      GitStatus is a ready-to-commit repo (is_repo + 15 changed files); screenshot-validated (both
      menu items, Enable Git correctly greyed with no project open).
    - `[x]` **3 — stage + commit LANDED** (`Source/Git.cpp`, commit): `apiGitAdd(dir, paths)`
      (`git add`; empty paths → `-A` stage-all) + `apiGitCommit(dir, message, amend)`
      (`git commit -m`, `--amend`, `--amend --no-edit` when amending with no new message).
      Commit reuses the user's own git identity, falling back to a neutral `-c user.email/name`
      **only when none is configured** (so a fresh machine / CI can commit without clobbering a
      real id). Empty non-amend message is rejected. GitAdd/GitCommit RPCs (GitAddRequest/
      GitCommitRequest) + Python `git_add`/`git_commit`. **Desktop:** File → "Commit..."
      (`showCommitDialog`) saves the composition, lists the changed files with their porcelain
      status codes, and opens an AlertWindow with a message field + Commit / Amend / Cancel;
      refreshes the Source Control window after. smoke: GitAdd → GitCommit "smoke commit" → the
      message is in `git log`, the tree goes clean, and an empty-message commit is rejected.
      Screenshot-validated end-to-end (the dialog listing `M gloopy.toml` / `M mixer/inserts.toml`
      / `M params.toml` / `?? tracks/bass.toml` for a newly-added track — the readable-diff payoff).
      **Not yet:** per-file / hunk staging (dialog stages all for now); a multiline message editor.
    - `[x]` **4 — history / commit graph LANDED** (`Source/Git.cpp`, commit): `apiGitLog(dir, max)`
      → `GitCommitInfo{hash, parents[], refs, author, date, subject}` newest-first, parsed from
      `git log --pretty=format` with a `0x1f` unit-separator between fields (so subjects can't collide
      with the delimiter) + `--date=short`. GitLog RPC (GitLogRequest/GitLogResult/GitCommitInfo) +
      Python `git_log`. **Desktop:** File → "History..." (`openHistory`) opens a read-only window
      listing each commit (hash, `(refs)`, subject, author · date, merge indicator) — the seed of the
      full commit-graph panel. smoke: two commits → GitLog is newest-first with the newest's parents
      linking to the oldest's hash; screenshot-validated (a 2-commit repo showing `(HEAD -> main)`).
      **Not yet:** the DAG lines / per-commit diff / filter (a richer panel in a later pass).
    - `[x]` **5 — branches LANDED** (`Source/Git.cpp`, commit): `apiGitBranches(dir)`→{current,
      branches[]} + `apiGitBranchCreate/Checkout/Merge/BranchDelete/BranchRename` (thin runGit
      wrappers via a shared `gitWrite` helper that captures stderr). GitBranches/GitBranchCreate/
      GitCheckout/GitMerge/GitBranchDelete/GitBranchRename RPCs (`GitBranchList`/`GitBranchRequest`/
      `GitRefRequest`) + Python. **Desktop:** File → "Branches..." (`showBranchMenu`) opens a popup —
      "On branch: <current>", each branch (current ✓, click = checkout), New branch... (create +
      switch), Merge into <current> ▸, Delete branch ▸, Rename current. **Checkout + merge change
      the working tree, so they reload the project** (`openAny(currentProjectFile)`) and are
      **guarded against a dirty tree** (commit first — stash-choice is slice 9). smoke: create feat →
      checkout → commit-on-feat → checkout main → merge → GitLog shows the feature commit on main;
      screenshot-validated (the popup on a 2-branch repo, main ticked). Branches = alternate
      arrangements. **Not yet:** stash-on-dirty-checkout (slice 9), merge-conflict UI (slice 11).
    - `[x]` **6 — tags LANDED** (`Source/Git.cpp`, commit): `apiGitTags(dir)` (for-each-ref
      `refs/tags`, newest-created first) + `apiGitTagCreate(dir,name,message)` (lightweight when no
      message, else `-a -m` annotated with the same commit identity-fallback) + `apiGitTagDelete`.
      Checkout-by-tag reuses `apiGitCheckout` from slice 5. GitTags/GitTagCreate/GitTagDelete RPCs
      (`GitTagList`/`GitTagRequest`) + Python. **Desktop:** File → "Tags..." (`showTagMenu`) — "Tag
      this version..." (name + optional description → tag HEAD), each tag "Check out: <name>"
      (detached-HEAD checkout + reload + a "viewing tag, switch back via Branches" note; guarded on a
      dirty tree), and a Delete-tag submenu. smoke: tag HEAD → GitTags lists it → checkout by tag →
      GitStatus reports a detached HEAD; screenshot-validated (the popup on a tagged repo). Tags =
      milestone mixes (demo / master / album-cut).
    - `[x]` **7 — checkout any version LANDED** (`showVersionPicker`, commit): NO new API — reuses
      `apiGitCheckout` (slice 5) + the reload path (`openAny`/`LoadComposition`) + the
      GitBranches/GitTags/GitLog reads. **Desktop:** File → "Open at version..." opens a single
      picker over **Branches** (current ✓), **Tags**, and the **15 most recent commits** (hash +
      subject); selecting any checks it out and reloads the project at that revision. A tag/commit is
      a detached HEAD, so a **"← Return to <main/master/first branch>"** item appears at the top when
      detached, plus a "viewing '<ref>' (detached)" note after checkout. Guarded against a dirty
      tree. smoke (isolated with a SaveProject/LoadProject session snapshot, since it
      LoadComposition-swaps the live project): commit A (1 track) → edit → commit B (2 tracks) →
      checkout A + reload → the project MODEL has 1 track; back to tip → 2 tracks — proving the
      reload-at-revision ties git to the actual arrangement. Screenshot-validated (the picker's
      Branches/Tags/Recent-commits sections). The "audition an old cut" superpower.
    - `[x]` **8 — diff viewer LANDED** (`Source/Git.cpp` `apiGitDiff`, commit): `apiGitDiff(dir,
      pathspec, revA, revB)` → `{ok, error, diff (unified text), files[{status,path}]}`. Revision
      selector: both revs = between two revisions; revA only = revA..working tree; neither = working
      tree vs HEAD. Runs `git diff --name-status` (the changed-file summary) + `git diff` (the
      unified text). GitDiff RPC (`GitDiffRequest`/`GitDiffResult`/`GitDiffFile`) + Python
      `git_diff`. **Composition-text-aware:** in a Composition folder each path IS a track / clip /
      setting, so the file summary names *what* changed; the readable-TOML makes the hunks legible.
      **Desktop:** File → "Changes (Diff)..." (`openDiff`) — SaveComposition first (so the diff
      reflects in-memory edits), then a read-only window leading with "Changed since last commit (N):
      <status> <path>" and the unified diff. smoke: commit → edit `track/1/synth/cutoff` → commit →
      GitDiff revA..revB shows exactly `-value = 20000` / `+value = 900` (a hunk, ≥1 file). Isolated
      via a SaveProject/LoadProject snapshot. Screenshot-validated end-to-end (the window showing
      `M params.toml` / `M tracks/lead.toml` + the `-cutoff = 20000` / `+cutoff = 900` hunks — the
      readable-diff payoff). *Done-when met: an edited param → a minimal, correct two-commit diff.*
    - `[x]` **9 — discard / stash / revert / reset LANDED** (`Source/Git.cpp`, commit):
      `apiGitDiscard(dir,paths)` (`git checkout -- <paths|.>`, tracked only — untracked files are
      left, discarding them = silent data loss), `apiGitStash(dir,msg)` / `apiGitStashPop` /
      `apiGitStashList` (`git stash push/pop/list`), `apiGitRevert(dir,commit)` (`git revert
      --no-edit`, with the commit identity-fallback), `apiGitReset(dir,mode,ref)` (`git reset
      --soft|mixed|hard`, validated). RPCs GitDiscard/GitStash/GitStashPop/GitStashList/GitRevert/
      GitReset (`GitPathsRequest`/`GitStashRequest`/`GitStashListResult`/`GitResetRequest`; note the
      message is `GitStashListResult` — a message named `GitStashList` collides with the RPC method
      name) + Python `git_discard`/`git_stash`/`git_stash_pop`/`git_stash_list`/`git_revert`/
      `git_reset`. **Desktop:** File → "Discard / Stash / Reset..." (`showWorkingTreeMenu`) — Discard
      all (confirm), Stash changes, Pop latest stash (enabled iff a stash exists), Revert a commit ▸
      (recent 10), Reset to a commit ▸ (each with Soft/Mixed/Hard; **hard double-confirmed**). All
      reload the project after (they rewrite the tree). smoke (isolated via a SaveProject/LoadProject
      snapshot): stash round-trip (dirty 2 → clean 0 → dirty 2 via GitStatus change counts) + revert
      (commit cutoff=900 → GitRevert HEAD → the on-disk `tracks/*.toml` is back to `cutoff = 20000`).
      Screenshot-validated (the popup). *Done-when met: stash→clean→pop→changes return; revert undoes
      a commit.*
    - `[x]` **10 — remote + fetch / pull / push / sync LANDED** (`Source/Git.cpp`, commit):
      `apiGitAddRemote(dir,name,url)`, `apiGitListRemotes` (parses `git remote -v`, fetch rows →
      `GitRemote{name,url}`), `apiGitFetch(dir,remote)` (empty = `--all`), `apiGitPull(dir,remote,
      branch)`, `apiGitPush(dir,remote,branch)` (`push -u` to set upstream). RPCs GitAddRemote/
      GitListRemotes/GitFetch/GitPull/GitPush (`GitAddRemoteRequest`/`GitRemoteList`/`GitSyncRequest`)
      + Python `git_add_remote`/`git_list_remotes`/`git_fetch`/`git_pull`/`git_push`. **Desktop:**
      File → "Remotes / Push / Pull..." (`showRemoteMenu`) — lists each remote (name → url), "Add
      remote..." (name+url prompt), Fetch/Pull/Push from/to `<remote>/<branch>` with **↑ahead ↓behind**
      counts from GitStatus. Network ops run off the message thread via `runBackground` + the busy
      overlay (reusing the user's SSH/credential helper); a Pull reloads the project after; **Push is
      always explicit** (never automatic). smoke (isolated via a SaveProject/LoadProject snapshot):
      add a remote pointing at a **bare local repo** (`git init --bare`, no network) → GitPush →
      `git -C <bare> log` shows the commit + ListRemotes returns origin. Screenshot-validated (the
      popup: origin → url, Add remote, Fetch/Pull/Push). *Done-when met: push to a bare local repo
      lands the commit there.*
    - `[ ]` **11 — merge-conflict resolution.** When a merge / pull conflicts, surface the conflicted
      composition files (TOML / `.notes`) with **accept-ours / accept-theirs / keep-both**, mark
      resolved, continue the merge, reload. The composition text format makes conflicts human-
      readable — lean into that rather than shelling users out to a terminal. *Done when:* a
      hand-built conflicting merge is resolved in-app and the merge completes, headless.
    - `[ ]` **12 — config + polish.** Per-project identity override (user.name / user.email),
      **auto-stage-on-save / auto-commit-on-save** toggles (opt-in only), **Git LFS** setup for large
      WAV / plugin sidecars, commit-message templates, and file-history / blame (stretch). Rounds out
      the IDE-grade surface.

### Wave 10 — Build, platform & toolchain ✦

Cross-cutting infrastructure — no user-facing model, but it widens who can run Gloopy and keeps
the foundation current. Both items are ✦ design forks (toolchain decisions) and are
**headless-verifiable by construction**: a green build on the target + `tests/smoke.sh` passing
is the proof.

31. **Upgrade JUCE to the latest release.** ✦ **M**
    JUCE is pinned at **8.0.15** via `FetchContent` (`CMakeLists.txt:19`, `GIT_TAG 8.0.15`).
    Track the newest stable JUCE tag (latest 8.x today; move to 9.x when it ships). *Approach:*
    bump `GIT_TAG`, rebuild, and shake out deprecations/API breaks across our JUCE surface
    (LookAndFeel/`drawButtonBackground`, `ChildProcess`, `AudioFormat*`, plugin-hosting
    `AudioPluginFormatManager` / VST3+LV2, `ValueTree` serialisation, the FileChooser/threading
    idioms). Watch the embedded **surge**, which vendors its *own* JUCE under
    `third_party/surge/libs/JUCE` — our upgrade only moves the FetchContent copy; keep surge's
    pinned to what it expects (don't force-share a JUCE). *Done when:* a
    clean `cmake --build` is green on the bumped tag, `ctest` + `tests/smoke.sh` pass, and the
    GUI renders (Xvfb screenshot) with no new warnings we introduced.
32. **Windows build (cross-build or CI-native).** ✦ **L**
    Gloopy is Linux-first today; make a Windows binary. *✦ Fork:* **(a) true cross-compile from
    Linux with `mingw-w64`** (a CMake toolchain file + cross-built gRPC/protobuf, sfizz, surge,
    JUCE) — self-contained but the dependency chain (gRPC especially) is painful to cross-build;
    or **(b) CI-native Windows build** (a GitHub-Actions `windows` runner + MSVC/clang-cl, `vcpkg`
    or FetchContent for gRPC/protobuf) — the pragmatic path, and it also gets us Windows CI.
    **Recommend (b) first** (real Windows toolchain, standard for JUCE apps), keep (a) as a
    stretch. Platform notes to resolve in the slice: **VST3 is the Windows plugin format** — set
    `JUCE_PLUGINHOST_VST3=1`, likely `JUCE_PLUGINHOST_LV2=0` on Windows (LV2 hosting is niche
    there); path handling (backslashes, `%APPDATA%` for presets/plugin cache) already goes through
    `juce::File`, but audit our env-var / sample-path resolvers; the **git-PM epic (#30)** must
    find `git.exe` (detection already probes `PATH`). *Done when:* a Windows Gloopy.exe builds in
    CI, boots, hosts a VST3, and renders a non-silent WAV (the smoke assertion) on Windows.

### Wave 11 — MCP service (AI agents drive Gloopy over stdio) ✦

A **stdio MCP (Model Context Protocol) server** so AI agents — Claude Desktop / Claude Code and
any MCP client — drive Gloopy directly, with no custom client glue. Squarely on the north star
(the GUI is one client among scripts, CI, and agents) and it *is* principle 5, not a violation:
the MCP server is a **thin adapter over the existing gRPC control API**, not new engine logic or a
second scripting VM. Transport = **stdio** (JSON-RPC over stdin/stdout — the local-subprocess
standard an MCP client launches with a `command` + `args`). Promotes the MCP bullet out of
Wave 6 #20.

**✦ Design fork — a Python MCP server over the existing gRPC client, first.** Reuse
`python/gloopy/client.py` + the official MCP Python SDK (FastMCP); ship under `python/gloopy-mcp/`
with a documented registration snippet. Keep a native **`gloopy mcp` stdio subcommand** (MCP framing
in C++, zero Python runtime) as a later self-contained option. Either way it's a translation layer:
MCP tool call → gRPC RPC on a Gloopy instance.

**Connection modes:** **attach** to a running Gloopy (`127.0.0.1:50051` — the GUI the user already
has open, so the agent and the human share one project) **or spawn a headless** instance for
batch / agent-only work.

**Tool surface — canonical, task-shaped, mapping to existing RPCs** (the Ardour #5 shape):
`session/get_info`, `tracks/list`, `track/add`, `clip/add`, `clip/move`, `midi_note/import_json`
+ `export_json` (bulk generative note I/O), `markers/add_range`, `transport/{play,stop,set_tempo}`,
`render/preset`. Stable IDs from the composition model; every mutating tool is **undoable** in
Gloopy. Keep outward-facing ops (git push, arbitrary file writes) **out of the initial surface** —
gate them later.

33. **MCP stdio service.** ✦ **L (multi-session)** *(thin gRPC adapter; each slice is provable by
    piping a JSON-RPC sequence into the server over stdio and asserting the reply — scriptable,
    headless.)*
    - `[ ]` **1 — stdio skeleton + tool discovery.** A FastMCP server that connects to a running
      Gloopy over gRPC; `initialize` + `tools/list` return the canonical tools; a read-only handful
      wired (`session/get_info`, `tracks/list`, `transport/set_tempo`). *Done when:* piping
      initialize → tools/list → tools/call over stdio returns live state, headless.
    - `[ ]` **2 — mutating tools.** `track/add`, `clip/add` (from a JSON note list), `clip/move`,
      `markers/add_range` — each a thin gRPC call, undoable. *Done when:* an agent transcript builds
      a 2-track loop and a render proves it non-silent.
    - `[ ]` **3 — bulk generative note I/O.** `midi_note/import_json` + `export_json` (reuse
      `ImportNotesJSON` / `ExportNotesJSON`). *Done when:* import a JSON melody → clip → render;
      export round-trips.
    - `[ ]` **4 — render / export tools.** `render/preset` (RunExport by name), range / stem render;
      returns the file path + a loudness report (reuse the analyze path). *Done when:* `render/preset`
      writes the expected WAV and returns its path, headless.
    - `[ ]` **5 — resources + spawn-headless mode.** Expose the current composition (and `docs`
      model) as MCP **resources** (readable agent context); a `--headless` mode that spawns its own
      Gloopy for agent-only / batch runs. *Done when:* `resources/list` returns the composition and
      headless mode drives a render with no GUI.
    - `[ ]` **6 — packaging + registration (+ native subcommand, stretch).** A `gloopy-mcp` entry
      point + a documented Claude Desktop / Claude Code MCP config snippet; (stretch) the native
      `gloopy mcp` stdio subcommand for a Python-free path. *Done when:* a stock MCP client registers
      the server from the documented snippet and lists Gloopy's tools.

## Explicitly NOT doing (the guardrails, made concrete)

- No full Ardour-style recording/post-production workflow; no general patchbay until
  buses/sends (#7) actually demand it.
- No hardware-specific C++ subsystem per controller — mappings stay data-driven (#19).
- No embedded Lua or second scripting VM — scripts drive the existing gRPC API.
- No built-in-effect catalog — keep the set small and high-value (#15); Gloopy hosts
  VST3/LV2 for the long tail.
- No broad foreign-format support before the composition-as-repo format is rock-solid.
- No reimplementing git or vendoring libgit2 (#30) — **detect and shell out to the system
  `git`**, reusing the user's identity/SSH/credentials. Git state lives in `.git`, never in the
  composition format. **Never auto-push** — push is always an explicit user action.
- **Never** copy third-party code/assets without an explicit AGPL-3.0 license review
  (Ardour is GPL), and **never** name other DAWs/trackers as the source in code, docs,
  or commit messages — borrow the idea, not the brand.

## How to run a grind session

1. `cd ~/git/gloopy`; read `AGENTS.md` and `git log --oneline -15`. If
   `docs/ROADMAP.md` records a half-built slice, resume exactly there.
2. Pick the next slice from **the ★ near-term order banner**, not by naïve wave number —
   the current directive is **Phase A (Waves 9 → 10 → 11) first, then back to the beginning**
   (Phase B). Only fall back to "top-down within the lowest incomplete wave" once the banner's
   explicit order is exhausted. If it's a ✦ design fork, study the prior art's *shape* first,
   then design the small Gloopy version — adapt, don't copy, and mind the AGPL/GPL boundary.
3. Implement the smallest coherent slice across the full stack (model → serialise →
   proto+gRPC → OSC → clients → CMake). Build. Boot ONE instance
   (`pkill -x Gloopy` first). Drive it over `grpcurl`, round-trip through the
   composition, `RenderToFile`, and assert the WAV/JSON in a script. Add the assertion
   to `tests/smoke.sh` (renders) or `GloopyTests` (pure logic).
4. Green build + `ctest` + `tests/smoke.sh` before committing. One slice per commit
   (co-author trailer), pushed to `origin` right away. **Document the new capability in the
   user-facing manual (`docs/`, `mkdocs build --strict` green)** — part of the slice, not a
   follow-up. Update `docs/ROADMAP.md` and check the item off in this skill.
5. If the slice is large and the session is ending, stop cleanly: commit the green
   sub-step, checkpoint the resumption state in `docs/ROADMAP.md`, hand off. Multi-
   session slices are normal — don't fake completion.
6. Repeat. Gloopy gets deeper one API-reachable, headless-proven, composition-stable
   slice at a time.
