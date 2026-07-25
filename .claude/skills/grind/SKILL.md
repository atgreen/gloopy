---
name: grind
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

So the model layers (tracks, mixer inserts, effects, automation, plugins, sfizz,
composition I/O, recording, presets, the OSC/gRPC surface) all exist. The backlog
below is about *depth, routing, musical model, analysis, and product surface* — not
foundational plumbing.

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
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`. No git
  remote — commits are local. Commit only a coherent, verified slice; branch first
  only if asked. Update `docs/ROADMAP.md` (or a new design doc) with the feature's
  goal/approach, and check the item off in this skill's backlog.
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
   - `[~]` **Minimal slice landed** (`Source/Parameters.cpp`, commit): flat string-id
     read/write layer — `ListParameters`/`GetParameter`/`SetParameter` RPCs +
     `ParameterInfo{id,name,value,min,max,default_value,unit,scaling}`, delegating to
     the existing thread-safe setters. Covers `track/<id>/{volume,pan,mute,solo}`,
     `track/<id>/synth/<name>`, `insert/<i>/{volume,pan,mute,solo}`,
     `effect/<i>/<slot>/<param>`. Proven via smoke.sh (102 params; cutoff set/get) +
     grpcurl. **Still to do for full done:** plugin-param ids; make automation/OSC
     *address these same ids* (today they still use the `AutoTarget` tuple / OSC
     lanes); persist a param snapshot in the composition; log/dB scaling used by UI.

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
     **Not yet:** folding the *existing* SetLoop/punch state into this model (today
     they're still separate); ruler UI; export/skip semantics.

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
     non-silent. **Not yet:** bulk `ImportNotesJSON`/`ExportNotesJSON` (generative
     helper — `AddClip` already takes structured notes), loop-expansion on export,
     multi-channel/CC import, per-clip (not per-track) granularity.

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
     **Not yet:** level-matched freeze (pre-master capture); split-at-named-marker; fade
     curve shapes (linear only); audio-clip consolidate (audio clips are one-shot).

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
     **Not yet:** pre/post-fader send choice, send presets, control groups; the mixer
     bus/send management UI (whole subsystem is API-only today).

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
     **Not yet:** control groups (VCA-lite fader scaling).

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
     the render and `sync_beats` round-trips. **Not yet:** envelope sources, phase offset,
     unipolar/smoothing, multiple LFOs per target, plugin-param targets.

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
      zero-crossing rate (ratio 1.999) and the value round-trips. **Not yet:** SFZ/plugin
      microtuning, per-note (scale) tuning tables, Scala/`.kbm` import, piano-roll UI.

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
      of these, transient/onset detection.

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
      **Not yet:** multi-band EQ; analyzers (scope/spectrum/vectorscope) with API
      snapshots.

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
  (commit efac838). Screenshot + headless validated. Follow-up: a full mod-matrix VIEW
  (ListModulations) if managing many routes becomes fiddly.
- `[x]` **MIDI-learn** (AddControllerMap/MidiLearn) — right-click a mixer fader / pan /
  FX param knob → MIDI Learn → apiMidiLearn(target) (commit 8567627). Screenshot +
  headless validated (cc:20 → insert/0/volume). Still open: a full mapping-rack VIEW
  (list/edit/remove existing maps, inversion/smoothing/bypass) — #19.
- `[x]` **Tempo markers** (AddTempoMarker/RemoveTempoMarker) — right-click the beat ruler
  → Add (BPM prompt) / Remove; markers drawn as flags on the ruler (commit 004c19f).
  Screenshot + headless validated (bar 3 / 150 BPM).
- `[x]` **Punch range** (SetPunchRange) — Alt-drag the ruler to set the punch in/out
  region; drawn in red (loop stays plain-drag/accent) (commit 71e31ef). Screenshot-
  validated (RPC-set bars 2-4 + Alt-drag bars 5-7).
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

16. **Browser sidebar + demo/template browser + `File → New From Template`** *(Idea
    #1/#2; absorbs remaining preset UI/work).* **L**
    - `[~]` **Empty-by-default + template system landed** (commit 5b90d3b, user request):
      new/launched projects are EMPTY (the forced Kick/Snare/Hat/Clap/Bass kit is gone).
      `buildTemplate`/`builtinTemplateNames` (Starter Beat / Drum Kit / Lead + Bass) +
      apiListTemplates/apiNewFromTemplate + ListTemplates/NewFromTemplate RPCs + Python +
      `File → New from Template`. Track ids are now 1-based (proto3 omits id 0). **The
      browser sidebar should drive project seeding through this same template API.** Still
      to build: the sidebar itself + file-based/user templates ("Save as Template").
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
      (0.1→0.0, 60→72, jitter ≤±0.02). **UI needs visual eval.** **Not yet:**
      knife/strum tools, ghost notes, step recording, velocity-tool
      drag, per-note selection ops.
    - `[x]` **Scale highlighting landed + screenshot-validated** (commit): `PianoRoll::
      setScale(root,intervals)` builds a 12-pitch-class mask; `paint()` tints in-scale
      rows (chromatic ⇒ off); wired from `apiSetScale` + `refreshUiAfterLoad`. Verified
      by offscreen screenshot (Xvfb capture) — bumped tint 0.10→0.22 alpha for
      readability after the shot showed it too faint.
    - `[x]` **Strum landed** (commit a4e353e): shared strumNotes transform + apiStrumClip +
      StrumClip RPC + STRUM header button (Shift-click = up) + S/Shift+S keys. Smoke +
      screenshot validated.
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
      Screenshot + functional validated. **Not yet:** knife/split tool, step recording.
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
      that bypass freezes the param while un-bypass re-applies. **Not yet:** OSC-lane
      wiring, per-map smoothing (slew), device-map files, the mapping-rack UI.
20. **Product-surface tier** — in-app markdown **project notes** under `notes/`
    (Idea #10); a static-file **localhost web control surface** for transport/mixer/
    markers/live notes, doubling as an API test client (Ardour #7); an **MCP tool
    surface** (`session/get_info`, `tracks/list`, `track/add`, `clip/move`,
    `midi_note/import_json`, `markers/add_range`, `render/preset`) alongside gRPC/OSC
    (Ardour #5); a **script browser** exposing composition-local + built-in scripts as
    installable actions that run against the gRPC API — not an embedded VM (Ardour #4).
    **L**, each independent.
    - `[~]` **Project notes landed** (`Source/Notes.cpp`, commit): free-form markdown
      (`projectNotes`) with `SetProjectNotes`/`GetProjectNotes` RPCs + Python client;
      serialised on the root ValueTree + composition `notes.md` (readable markdown,
      manifest-referenced). UI: File → "Project Notes..." opens a `TextEditor` window
      synced to the model. NewProject clears it. Verified headless: set → save →
      NewProject clears → reload restores. **UI needs visual eval.** **Not yet:** web
      surface, MCP surface, script browser (each still independent L slices);
      notes/ subdir for multiple docs (lyrics/credits split).

## Explicitly NOT doing (the guardrails, made concrete)

- No full Ardour-style recording/post-production workflow; no general patchbay until
  buses/sends (#7) actually demand it.
- No hardware-specific C++ subsystem per controller — mappings stay data-driven (#19).
- No embedded Lua or second scripting VM — scripts drive the existing gRPC API.
- No built-in-effect catalog — keep the set small and high-value (#15); Gloopy hosts
  VST3/LV2 for the long tail.
- No broad foreign-format support before the composition-as-repo format is rock-solid.
- **Never** copy third-party code/assets without an explicit AGPL-3.0 license review
  (Ardour is GPL), and **never** name other DAWs/trackers as the source in code, docs,
  or commit messages — borrow the idea, not the brand.

## How to run a grind session

1. `cd ~/git/gloopy`; read `AGENTS.md` and `git log --oneline -15`. If
   `docs/ROADMAP.md` records a half-built slice, resume exactly there.
2. Pick the next unchecked backlog item (top-down within the lowest incomplete wave).
   If it's a ✦ design fork, study the prior art's *shape* first, then design the small
   Gloopy version — adapt, don't copy, and mind the AGPL/GPL boundary.
3. Implement the smallest coherent slice across the full stack (model → serialise →
   proto+gRPC → OSC → clients → CMake). Build. Boot ONE instance
   (`pkill -x Gloopy` first). Drive it over `grpcurl`, round-trip through the
   composition, `RenderToFile`, and assert the WAV/JSON in a script. Add the assertion
   to `tests/smoke.sh` (renders) or `GloopyTests` (pure logic).
4. Green build + `ctest` + `tests/smoke.sh` before committing. One slice per commit
   (co-author trailer). Update `docs/ROADMAP.md` and check the item off in this skill.
5. If the slice is large and the session is ending, stop cleanly: commit the green
   sub-step, checkpoint the resumption state in `docs/ROADMAP.md`, hand off. Multi-
   session slices are normal — don't fake completion.
6. Repeat. Gloopy gets deeper one API-reachable, headless-proven, composition-stable
   slice at a time.
