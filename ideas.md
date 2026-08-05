# Feature Ideas

Product and architecture ideas that fit Gloopy's current direction: a compact
JUCE DAW with plugin hosting, composition-as-code projects, OSC/gRPC control,
automation, examples, rendering, and scriptable workflows.

Gloopy should stay smaller, more scriptable, and more composition-repo friendly
than a typical full DAW — these ideas are chosen with that in mind.

## Highest Leverage

### 1. Browser sidebar

A collapsible left-side browser for plugins, favorites, projects, samples,
presets, home, and root directories.

For Gloopy:

- Add a collapsible browser with tabs for Plugins, Samples, Projects, Presets,
  Compositions, Demos, and Favorites.
- Make drag-and-drop first-class: sample to sampler/audio track, plugin to track
  or mixer insert, composition/demo to open.
- Index both user folders and bundled content.
- Let composition folders expose `notes/`, `scripts/`, `exports/`, and stems
  without leaving the app.

### 2. Templates, demos, and presets as product surface

Gloopy already has examples, but they are mostly file-level assets.

For Gloopy:

- Add `File -> New From Template`.
- Add an in-app demo browser with one-click load.
- Add track/generator/effect-chain presets.
- Save presets in the composition-friendly text format where possible.
- Treat bundled examples as regression fixtures and user-facing starter songs.

### 3. Universal parameter model

A single parameter abstraction readable by UI widgets, automation, and
controllers. Gloopy already has OSC/gRPC, automation, mixer params, effect
params, and plugin params, so unifying them would pay off.

For Gloopy:

- Introduce a `ParameterRef` / `ParamModel` layer with id, name, min, max,
  default, scaling, unit, current value, and thread policy.
- Use it for UI knobs, automation lanes, MIDI/OSC mapping, gRPC state, plugin
  generic editors, and save/load.
- Support linear, logarithmic, and decibel scaling.
- Make parameter IDs stable enough for composition files and external clients.

### 4. Controller rack, MIDI learn, and parameter linking

Separate controllers from targets through controller connections. Gloopy's
control API makes this especially relevant.

For Gloopy:

- Add a controller/mapping view showing every source-to-target link.
- Support MIDI learn for any exposed parameter.
- Support OSC/API sources as mappable controllers, not just direct setters.
- Allow scaling, inversion, smoothing, range limiting, and bypass per mapping.
- Persist mappings in composition files.

### 5. Stronger piano-roll editing modes

Editing modes such as draw, erase, select, detune, knife, and strum, plus ghost
notes and scale/chord marking.

For Gloopy:

- Add knife/split, strum, duplicate, transpose, quantize, humanize, and velocity
  tools.
- Show ghost notes from other clips or tracks in the same time range.
- Add scale highlighting and optional "snap to scale".
- Add step recording from MIDI keyboard.
- Keep the step grid and piano roll editing the same note model.

## Good Next Tier

### 6. Richer sampler controls

Sample start/end, loop point, reverse, stutter, interpolation, and gain.

For Gloopy:

- Add start/end trim handles.
- Add reverse, loop modes, one-shot/hold behavior, root note, choke group,
  fade-in/fade-out, and interpolation quality.
- Show waveform handles directly in the sampler UI.
- Expose all controls through the parameter model and control API.

### 7. Cached waveform thumbnails

A dedicated sample-thumbnail cache for fast waveform visualization.

For Gloopy:

- Cache min/max peaks at multiple resolutions per audio file.
- Reuse the cache in audio clips, sampler UI, sample browser, and exports.
- Key cache entries by path, mtime, size, and eventually content hash.
- Keep generated caches outside committed composition data unless explicitly
  requested.

### 8. Selective built-in effects and analyzers

Add only the built-ins that improve everyday usefulness without becoming a plugin
museum.

Good candidates:

- Compressor / limiter
- Parametric EQ
- Bitcrusher
- Chorus / flanger
- Stereo widener
- Waveshaper / soft clipper
- Oscilloscope
- Spectrum analyzer
- Vectorscope

### 9. Import/export polish

Gloopy already has render support, region/stem ideas, and scriptable control.

For Gloopy:

- Add MIDI import/export.
- Add export presets: full mix, stems, selected region, selected tracks.
- Add one-click "render composition exports/".
- Add import filters for common sources: MIDI files, Hydrogen drum patterns, and
  SFZ folders.

### 10. Project notes

Gloopy's composition-as-code format makes project notes more natural than a
hidden project blob.

For Gloopy:

- Add an in-app markdown notes panel.
- Store notes under `notes/` in composition directories.
- Support arrangement notes, lyrics, TODOs, credits, sample licenses, and script
  notes.
- Include notes in the browser sidebar.

## More Specialized

### 11. Microtuning and scales

Explicit scale information is a strong fit for Gloopy because external scripts and
generative clients benefit from it.

For Gloopy:

- Add project-level scale definitions.
- Add per-track tuning mode.
- Support Scala/keymap import later.
- Use scales in piano-roll highlighting, API helpers, and generative examples.

### 12. Envelope/LFO modulation blocks

Gloopy already has synth LFO work and automation; the next step is a small
modulation matrix.

For Gloopy:

- Let LFO/envelope generators target any `ParamModel`.
- Support tempo sync, phase, shape, depth, smoothing, and bipolar/unipolar mode.
- Persist modulation routes as readable composition files.
- Expose modulation routing through gRPC.

### 13. Visual analyzers as mixer tools

Oscilloscope, spectrum, and vectorscope tools are useful for mixing, debugging,
demos, and teaching.

For Gloopy:

- Implement them as built-in insert tools.
- Make them non-mutating effects in the chain.
- Add API-visible meter/analyzer snapshots only if useful for external clients.

### 14. Import filters

Keep the import architecture small and focused.

For Gloopy:

- Define an importer interface that converts foreign formats into the runtime
  model or composition directory layout.
- Start with MIDI and Hydrogen.

## Things To Keep In Check

- Keep built-in instruments/effects curated — Gloopy already hosts VST3/LV2, so
  built-ins should stay a small, high-value set rather than a catalog.
- Keep the JUCE layout simple; avoid a window-heavy workspace.
- Don't chase broad format support before the composition-as-code format is solid.
- Never copy third-party code or assets without a deliberate license review
  against Gloopy's AGPL-3.0 license.

## Suggested Implementation Order

1. Browser sidebar + demo/template browser.
2. Preset saving/loading for tracks, synths, samplers, and effect chains.
3. Universal parameter model.
4. Controller rack / MIDI learn.
5. Piano-roll editing upgrades.
6. Richer sampler controls and cached waveforms.
7. Export/import polish.
8. Project notes.
9. Microtuning and modulation matrix.
10. Visual analyzers.

---

# Ideas Borrowed From Ardour

Ardour is a deeper recording, editing, routing, and mixing workstation than
Gloopy needs to become. The useful borrowings are mostly systems and workflow
patterns: session discipline, export profiles, marker/range handling, control
surfaces, scripting, routing clarity, and offline tools.

## Highest Leverage

### 1. Session/composition utilities

Ardour has command-line session utilities that use the same core engine as the
DAW (`session_utils/README`). The stated goal is non-interactive Unix-style
tools installed alongside the DAW.

For Gloopy:

- Add small CLI tools or subcommands for common composition tasks:
  - `gloopy inspect <project-or-composition>`
  - `gloopy render <composition> --preset streaming`
  - `gloopy export-stems <composition>`
  - `gloopy validate <composition>`
  - `gloopy pack <composition> out.zip`
- Reuse the same load/render code paths as the GUI.
- Make these tools CI-friendly for examples and composition repositories.
- Prefer predictable stdout/JSON output so scripts and agents can consume it.

### 2. Export profiles and named render targets

Ardour treats export as a profile-managed system, with regular, range,
selection, region, and stem exports (`ExportProfileManager`) plus installed
format/preset files under `share/export`.

For Gloopy:

- Add named export presets stored in composition text files.
- Support targets such as full mix, selected range, current clip, selected
  tracks, stems, and loop region.
- Include delivery presets: WAV master, FLAC archive, streaming WAV/MP3, loop
  pack, and stems.
- Put default exports under `exports/` and make filenames deterministic.
- Let gRPC and Python/Lisp clients invoke presets by name.

### 3. Markers, ranges, sections, loop, and punch

Ardour's `Location` model unifies markers, ranges, loop ranges, punch ranges,
CD markers, cue markers, sections, scenes, skip ranges, and clock origins.

For Gloopy:

- Add a project-level `TimelineLocation` model:
  - marker
  - range
  - loop range
  - punch range
  - arrangement section
  - export range
  - skip/mute range
- Store locations in composition files as readable TOML.
- Surface them in the ruler and control API.
- Let render presets target named ranges such as `intro`, `chorus-2`, or
  `loop-a`.

### 4. Script library as product surface

Ardour ships many Lua scripts under `share/scripts` and documents naming
conventions for examples, snippets, and scripts. The important lesson is not Lua
specifically; it is that scripts are installed, browsable, reusable content.

For Gloopy:

- Add a `scripts/` browser for composition-local and built-in scripts.
- Treat Python and Common Lisp examples as installable actions, not just docs.
- Add a script manifest with name, description, parameters, and entry point.
- Let scripts run against the existing gRPC API instead of embedding another
  scripting VM.
- Ship scripts for common tasks: create drum tracks, normalize velocities,
  generate arps, render stems, add common effect chains, and validate a
  composition.

### 5. MCP/AI control surface shape

Ardour has an experimental MCP HTTP surface with practical tool groups:
session, transport, tracks/buses, sends/routing, plugins, markers/ranges,
regions, and MIDI notes.

For Gloopy:

- Consider an MCP surface alongside gRPC/OSC so AI tools can drive Gloopy
  without custom client glue.
- Keep tool names canonical and task-shaped:
  - `session/get_info`
  - `tracks/list`
  - `track/add`
  - `clip/move`
  - `midi_note/import_json`
  - `markers/add_range`
  - `render/preset`
- Add bulk JSON note import/export for fast generative workflows.
- Make all tools report stable IDs from the composition model.

## Good Next Tier

### 6. Control surfaces and hardware maps

Ardour has dedicated control-surface implementations and a large set of MIDI
maps under `share/midi_maps`.

For Gloopy:

- Add a simple device-map format for MIDI controllers.
- Ship generic maps for common small controllers first:
  - transport controls
  - selected track fader/pan/mute/solo
  - mixer bank controls
  - plugin parameter banks
  - drum pad note maps
- Let users bind buttons to named actions.
- Keep the mapping layer protocol-neutral so MIDI, OSC, and future MCP/web
  clients share the same action IDs.

### 7. Web control surface

Ardour includes web-surface infrastructure under `share/web_surfaces` and
`libs/surfaces/websockets`.

For Gloopy:

- Add a tiny localhost web control surface for transport, mixer, markers, and
  live notes.
- Use it as both a user feature and a test client for the control API.
- Keep it static-file based and driven by the API, not a second app framework.
- Make it useful on a phone or tablet for recording/performing away from the
  keyboard.

### 8. Routing clarity: buses, sends, returns, and route comments

Ardour's `Route` model is broad: inputs, outputs, processors, sends, returns,
monitoring, solo state, comments, groups, and meter points. Gloopy should not
copy that whole system, but the user-facing concepts are valuable.

For Gloopy:

- Add explicit bus tracks and send levels.
- Let tracks route to buses before master.
- Show signal flow clearly: generator -> track inserts -> sends -> bus inserts
  -> master.
- Add route comments/notes for mix decisions.
- Add simple send presets such as vocal reverb, drum parallel compression, and
  delay throw.

### 9. Mixer scenes and snapshots

Ardour's `MixerScene` snapshots controllable values and can recall them later.

For Gloopy:

- Add named mixer snapshots:
  - rough mix
  - vocal up
  - drums up
  - dry mix
  - print mix
- Snapshot faders, pans, mutes, solos, send levels, and effect bypass states.
- Store snapshots as small composition files.
- Let automation stay separate from snapshots.

### 10. VCA/group controls

Ardour has VCAs and route grouping. Gloopy can implement a smaller version.

For Gloopy:

- Add control groups for tracks/inserts.
- Support group fader, group mute/solo, and group color.
- Use this for drums, vocals, pads, effects, and stems.
- Keep it as control scaling, not extra audio routing, unless a real bus is
  needed.

### 11. Monitoring and recording polish

Ardour has explicit monitoring, record-enable, record-safe, punch, and transport
state concepts.

For Gloopy:

- Add per-track record arm and record safe.
- Add auto-punch using named ranges.
- Add input monitoring modes: off, auto, on.
- Add a big record/transport status view for recording from across the room.
- Mark xrun/dropout events on the timeline for debugging recordings.

### 12. Region/clip operations

Ardour's editor exposes detailed region operations: split, resize, gain,
normalize, move, copy, import, and selection/range actions.

For Gloopy:

- Add clip gain and clip normalization.
- Add split at playhead, split at markers, crop to selection, duplicate to grid,
  consolidate, reverse audio, and bounce-in-place.
- Add per-clip fades for audio clips.
- **Linked / pooled clips** — several placements sharing one pattern so an edit to
  one updates all (aliases / shared copies / pooled MIDI; a point of differentiation
  from Ableton/Bitwig, which lack it). Full data-model + serialization design in
  `linked-clips.md`.
- Expose these operations through the control API and future MCP surface.

## More Specialized

### 13. Tempo map and time domains

Ardour has a deep temporal layer for samples, beats, bars, timecode, tempo maps,
and domain conversion. Gloopy only needs a small subset.

For Gloopy:

- Add tempo/time-signature changes on the timeline.
- Store tempo markers in composition files.
- Keep note/clip data beat-based and audio rendering sample-based.
- Add API helpers for converting bars/beats to beats/seconds.

### 14. Audio analysis tasks

Ardour includes analyzers, LUFS/EBU R128 analysis, onset detection, and export
analysis.

For Gloopy:

- Add offline loudness analysis for rendered files.
- Add peak, true peak, RMS, and LUFS reporting.
- Add transient/onset detection for slicing audio clips later.
- Use analysis results in `gloopy validate` and render reports.

### 15. Plugin scanning and metadata cache

Ardour has dedicated plugin managers, scan results, metadata, and separate scan
utilities.

For Gloopy:

- Persist plugin scan results with plugin id, name, format, path, vendor,
  category, parameter summaries, and scan status.
- Record failed scans without blocking startup.
- Add a CLI scan command.
- Make plugin identity stable in composition files even if the plugin path
  moves.

### 16. Real-time safety diagnostics

Ardour's codebase is serious about process threads, ring buffers, RT-safe
deletion, disk I/O, DSP load, and backend handling.

For Gloopy:

- Add a small diagnostics panel:
  - audio callback time
  - DSP load
  - xruns/dropouts
  - plugin latency
  - render speed
  - audio-device settings
- Add assertions or debug counters for allocations/locks on the audio path.
- Include diagnostics in bug reports and CI smoke logs.

## Things To Keep In Check

- Do not turn Gloopy into Ardour's full recording/post-production workflow.
- Avoid a giant patchbay until buses/sends actually need it.
- Keep control surfaces data-driven where possible; do not add a hardware-specific
  C++ subsystem for every device.
- Prefer Gloopy's existing gRPC/OSC/Python/Lisp architecture over embedding Lua
  just because Ardour uses it.
- Treat Ardour code and assets as reference material only. Ardour is GPL, and
  Gloopy is AGPL; direct copying still needs an explicit license review.

## Suggested Ardour-Inspired Implementation Order

1. CLI composition utilities: inspect, validate, render, pack.
2. Export profiles and named render targets.
3. Markers/ranges/sections model.
4. Script browser/actions built on gRPC clients.
5. MCP tool surface with bulk MIDI JSON import/export.
6. MIDI device maps and action binding.
7. Bus/send routing.
8. Mixer scenes and control groups.
9. Recording polish: punch, monitoring, record safe.
10. Diagnostics and offline loudness/render reports.

---

## Session View (clip-launch grid) — epic

Ableton-style Session view alongside the linear Arrangement view, Tab to switch
(ref: ~/git/ableton-live-redesign, Nenad Milosevic). Columns = tracks, rows =
scenes, each cell = a clip slot you launch live; clips loop and trigger in sync.
Gloopy is well-placed: `Clip` already owns its content and loops
(`looped`/`contentLenBeats`), so a session clip is just a Clip in a scene slot
instead of on the timeline.

Locked scope (confirmed with the user, 2026-07-26):
- **Simplified grid** — the color-coded horizontal-bar Session view (case-study
  p23-24), not the dense icon version; less clutter, fits Gloopy's look.
- **Tab cycles** Arrange -> Session -> Mixer -> Arrange.
- **Per-track override playback** (full Ableton model): one shared transport;
  launching a session clip on a track overrides that track's arrangement
  playback while other tracks keep playing the arrangement; "Back to
  Arrangement" resumes all tracks. This is the core of slice 2.

Adopted defaults (Ableton semantics): global launch quantization (default 1 bar);
one playing clip per track (launching another stops it); scenes are global rows;
launch an empty slot on an armed track to record into it. Session launching is a
live/interactive feature — NOT on the offline-render path, so determinism is
unaffected (renders still come from the arrangement).

Slices (one green commit each):
1. **Model + persistence** — `Track::sessionSlots` (null = empty), a global
   `scenes` list; round-trip through `.gloopy` (ValueTree) and composition-repo
   TOML; unit tests. No engine, no UI.
2. **Launch engine** — audio-thread clip/scene launch state machine: pending →
   fires at next launch-quant boundary; per-track exclusivity; scene launch = all
   clips in a row; stop-clip / stop-all. Deterministic, unit-tested.
3. **Session UI + Tab switch** — SessionView component (grid of slots, launch
   triangles, scene-launch column), Tab toggles Arrange/Session, per-track mini
   mixer strip (sends/pan/fader/meters/solo/arm). Reuses mixer + clip editing.
4. **Session recording** — arm + launch empty slot records a looping clip.
5. **Control API** — OSC + gRPC launch clip/scene, stop, query session state.
6. **Polish** — clip colours, follow-actions, capture, quantize menu, "Guit
   Magic ••▶" style launch-mode indicators from the mockup.

# Ideas Borrowed From Radium

Distilled from studying the [radium](https://github.com/kmatheussen/radium) DAW/tracker
(Kjetil Matheussen, ~20 years, ~5000 files). Radium independently confirmed several Gloopy
choices (zero-alloc audio thread, name/path-addressed params, content-addressed versioned
project format). Full notes with file pointers into a local checkout live in `FOR-GLOOPY.md`.

## Highest Leverage

### 1. Plugin crash resilience — cheapest high-value win

Gloopy hosts VST3/LV2 in-process via JUCE; a misbehaving plugin can crash on *scan*
(killing startup) or on *load*, with no persistent record. Radium protects the host two
ways that cost little.

For Gloopy:

- **Out-of-process scanning + on-disk description cache.** Scan plugins in a
  `juce::ChildProcess` (JUCE's own scanner supports out-of-process via a dead-man's-pedal
  file); persist the `KnownPluginList` XML; rescan only on change. A segfault while probing
  takes down the scanner child, not the app.
- **Blacklist-before-load (no watchdog needed).** Write the plugin to a persistent
  blacklist file (e.g. `~/.config/gloopy/plugin-blacklist.txt`) *before*
  `createPluginInstance`, and clear it only *after* a successful open. A load-time crash
  leaves the entry behind, so the next launch skips it and shows "crashed last time —
  try again / remove" instead of an infinite crash loop.
- Target files: `Source/PluginHost.h` + wherever plugin instantiation happens.
- Scope note: protects *scan* and *first load*, NOT a running plugin mid-session. Full
  per-plugin process isolation is a much bigger project — defer unless hosting untrusted
  plugins becomes a goal.
- Scars to copy: keep host chrome/meters/atomics in a side-struct, not a plugin god-object;
  persist plugin state as chunk AND flat params together with a per-type `version` field.

### 2. Exact rational time in the stored model — costlier to defer

> **Status: DONE.** `Source/Rational.h` provides `BeatRatio` (gcd-reduced, signed int64;
> thirds sum to exactly 1) and `StaticRatio` (non-reduced — keeps 3/4 distinct from 6/8).
> `BeatRatio::fromBeats` uses continued-fraction recovery, so a stored `double` reloads as
> its exact minimal fraction (0.665 → 133/200, 0.333… → 1/3).
> - **Note::startBeat / lengthBeats** and **Clip::startBeat / lengthBeats / contentLenBeats**
>   are now `BeatRatio` — exact in memory; `.toBeats()` converts at the DSP/UI boundary.
> - **Exact quantize** snaps on rational grid math (idempotent, drift-free).
> - **Serialization** stays double-valued but round-trips the exact fraction via the CF
>   recovery (verified by the round-trip unit test), so no separate num/den format is needed.
> - Validated: full unit suite + smoke green.
>
> Still open (small, optional): time signatures / grid divisions on `StaticRatio` (they're
> still doubles), so `3/4` and `6/8` stay notationally distinct.


`Source/TimeTypes.h` wraps a bare `double` for stored positions. Doubles are right for DSP
and scheduling, but as the *stored* representation they drift (quantize, loop boundaries,
"is this exactly on the bar?"). Foundational, painful to retrofit once every clip/note/
marker position and `beatToSamples` site is a `double` — so decide the direction now, while
the `TimeTypes` migration is still incremental.

For Gloopy:

- Introduce a rational (or int64 PPQ-tick) type for the **stored** model — clip/note/marker
  positions and edit-time math (quantize, snap, loop points, equality). Radium uses
  gcd-reduced `Ratio { int64 num, den }`.
- Keep `double` beats/seconds for DSP and the scheduler; convert rational → double at the
  engine boundary (the existing `beatToSamples`/`TempoConv` layer is the natural seam).
- Add a **separate, deliberately non-reduced** rational for time signatures / grid divisions
  so `3/4 ≠ 6/8` stays notationally distinct (radium's `StaticRatio`).
- Use **signed** integer components (radium's own logged scar: an unsigned counter was
  flagged "extremely dangerous").
- Keep integer project-format versions (already true) and add a round-trip migration test
  per version bump so old time-format read paths stay exercised.

## Priority

**#1 (plugin crash resilience)** first — cheap, self-contained, immediate robustness; it
also refines Ardour idea #15 (plugin scanning / metadata cache) above. **#2 (rational time)**
is the strategic one — decide the direction now even if the migration lands incrementally,
before every position field is a `double`. (See also Ardour idea #13, tempo map / time
domains.)
