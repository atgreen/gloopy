# Gloop Compositions

> **Status: Phases 1–2 implemented.** `Source/Composition.cpp` writes a composition
> directory (`saveComposition`) and reads it back (`loadComposition`), using the
> existing `toValueTree`/`loadFromTree` as the intermediate so the runtime↔tree
> mapping is untouched.
>
> - **Phase 1:** the writer + loader, `SaveComposition`/`LoadComposition` RPCs.
> - **Phase 2:** first-class in the File menu (open a `.gloopy`, a composition
>   folder, or a `.zip`; save-as `.gloopy` or composition; format-aware Save);
>   **zip read** (flat or single-top-folder archives, unpacked to a temp
>   workspace); **dirty-file tracking** — `saveComposition` is content-addressed,
>   so a no-op re-save writes nothing, moving one fader rewrites one file, and
>   removed objects prune their files. `dir → runtime → dir` is byte-stable.
>
> `LoadProject` auto-detects a directory / `gloopy.toml` / `.zip`. `.gloopy` XML
> stays the default save format. TOML is a small hand-rolled subset (`Source/Toml.h`,
> unit-tested); effect params are flat keys under each `[[effects]]` entry rather
> than the `[effects.params]` subtable sketched below. Covered by a TOML unit test
> and composition round-trip / dirty-tracking / zip checks in `tests/smoke.sh`.
>
> Not yet: comment/hand-grouping preservation across edits to a *changed* file
> (unchanged files keep theirs), and Phase 3 (consolidate assets, zip write).

## Goal

Gloopy project files should support a "composition as code" workflow: a song is
a directory of small, human-readable text files plus referenced assets, designed
to be edited, reviewed, branched, merged, zipped, and shared with normal source
control tools.

The existing `.gloopy` XML file remains useful as a compact interchange and
legacy save format, but it is a poor primary authoring format:

- unrelated changes are hard to review because the whole project lives in one
  XML document;
- large embedded sample/plugin blobs make diffs noisy and undo snapshots costly;
- parallel work on arrangement, mixer, lyrics/notes, and sound design conflicts
  unnecessarily;
- a composition cannot naturally carry docs, stems, scratch exports, or scripts.

The repo format should make a Gloopy composition feel like a small software
project: clear layout, stable identifiers, line-oriented data, and predictable
generated output.

## Inputs Gloopy Should Open

Gloopy should be able to open any of these as a composition:

- a directory containing `gloopy.toml`;
- a Git repository whose root, or a subdirectory, contains `gloopy.toml`;
- a `.zip` archive containing the same directory structure;
- a legacy `.gloopy` XML file.

For a zip file, Gloopy can mount or unpack to a temporary read-only workspace,
then save-as to a directory when the user edits. A later version can write zip
archives directly, but the directory form is the canonical editable form.

## Format Principles

- **Text first.** Project metadata, tracks, clips, notes, mixer state, and
  automation should be stored as UTF-8 text.
- **One standard format.** Metadata is TOML, notes and automation points are
  line-oriented text, and large opaque payloads are binary sidecar files. Do not
  add parallel JSON/YAML variants.
- **Slug IDs.** Tracks, clips, inserts, effects, and automation lanes get stable
  human-readable slug IDs that do not depend on array position.
- **Small files.** Split files along ownership boundaries so common edits touch
  only one or two files.
- **Readable diffs.** Prefer line-oriented records for dense note/event data.
  Avoid pretty formats that reorder keys unpredictably.
- **Preserve comments.** User comments and hand grouping in text files are part
  of the authoring experience and should survive load/save.
- **References by default.** Audio samples, SFZ libraries, plugin binaries, and
  renders are referenced by path unless explicitly consolidated.
- **Binary assets are fine.** Samples, plugin state, stems, and renders should be
  stored as ordinary binary files, not base64 text.
- **Generated output is optional.** Renders, waveform caches, plugin scans, and
  thumbnails belong under ignored cache/export directories unless deliberately
  committed.
- **Round-trip safe.** Loading and saving without edits should not rewrite
  unrelated files or reorder records.

## Proposed Layout

```text
my-song/
  gloopy.toml
  tracks/
    kick.toml
    bass.toml
    strings.toml
  clips/
    kick/
      verse-1.notes
      chorus.notes
    bass/
      verse-1.notes
      chorus.notes
  mixer/
    inserts.toml
    effects/
      master-chain.toml
      bass-chain.toml
  automation/
    master-gain.points
    bass-filter-cutoff.points
  assets/
    samples/
    sfz/
  exports/
    mix.wav
    stems/
  notes/
    arrangement.md
  scripts/
    build-demo.py
```

Each composition has exactly one `gloopy.toml`. A Git repository may contain
multiple compositions in separate subdirectories, but each composition directory
is self-contained and has its own manifest. A minimal project can inline
everything or point to a single track file; larger songs can split aggressively.

`exports/` and cache directories should be ignored by default in a generated
`.gitignore`, unless the user chooses to commit stems or release bounces.

## Root Manifest

`gloopy.toml` is the entry point and should be boring to review:

```toml
format = "gloopy-composition"
version = 1
title = "Nocturne Sketch"
bpm = 76.0
swing = 0.5

[paths]
sample_roots = ["assets/samples", "~/sfz"]

[[tracks]]
id = "strings"
file = "tracks/strings.toml"

[[tracks]]
id = "piano"
file = "tracks/piano.toml"

[mixer]
file = "mixer/inserts.toml"
```

The manifest owns global tempo, path policy, track ordering, and top-level file
references. Track order belongs here so reordering tracks creates a small,
obvious diff. IDs are slug strings generated from names (`strings`,
`lead-vocal`, `master-gain`) and made unique with a numeric suffix when needed
(`strings-2`). Once assigned, an ID should not change just because the display
name changes.

## Track Files

Each track file describes the sound source, routing, and clip list for one
track:

```toml
id = "strings"
name = "Strings"
type = "instrument"
colour = "#c05a4b"
default_pitch = 60
volume = 0.72
pan = 0.0
mute = false
solo = false
mixer_insert = "strings"

[generator]
type = "sfz"
path = "Virtual-Playing-Orchestra3/Strings/all-strings-SEC-sustain.sfz"

[[clips]]
id = "strings-intro"
name = "Intro"
start = 0.0
length = 32.0
content_length = 16.0
looped = true
notes = "../clips/strings/intro.notes"
```

Generator state should stay readable where possible:

- built-in synth params are TOML keys;
- sampler/SFZ paths are path references;
- plugin identity and parameter snapshots are TOML;
- opaque plugin state, when unavoidable, lives in binary sidecar files under
  `plugins/state/` so normal track diffs stay readable.

## Notes Files

Dense MIDI note data should be line-oriented. A simple tab-separated format is
easier to diff and merge than nested XML:

```text
# pitch  start  length  velocity
60       0.0    1.0     0.82
64       1.0    1.0     0.78
67       2.0    2.0     0.80
```

Rules:

- fields are `pitch`, `start`, `length`, `velocity`;
- times are in beats relative to the clip;
- comments start with `#`;
- records should be sorted by `start`, then `pitch`, then `length` for generated
  files;
- the saver must preserve comments and blank-line grouping where possible. If a
  file has user comments, Gloopy should update the note records in place rather
  than regenerating the whole file from scratch.

This format can grow with optional columns, for example `channel`, `probability`,
or `micro_offset`, if Gloopy later adds humanize/probability tools.

## Mixer And Automation

Mixer state can be split into stable insert records:

```toml
[[inserts]]
id = "master"
name = "Master"
volume = 0.9
pan = 0.0
mute = false
solo = false
effects = "effects/master-chain.toml"

[[inserts]]
id = "strings"
name = "Strings"
volume = 0.8
pan = 0.0
mute = false
solo = false
effects = "effects/strings-chain.toml"
```

Effect chains should use stable effect IDs:

```toml
[[effects]]
id = "strings-reverb"
type = "Reverb"
bypassed = false

[effects.params]
Room = 0.82
Damp = 0.35
Wet = 0.42
```

Automation should be a line-oriented points file:

```text
# beat  value
0.0     -4.5
32.0    -2.0
64.0    -5.0
```

The lane target lives in a small TOML header or manifest entry:

```toml
[[lanes]]
id = "master-gain"
target = "effect_param"
insert = "master"
effect = "master-gain"
param = "Gain dB"
points = "master-gain.points"
```

## Assets

The repo format should distinguish three asset classes:

- **external references:** paths resolved through `sample_roots`,
  `$GLOOPY_SAMPLE_PATH`, project directory, and user defaults such as `~/sfz`;
- **project assets:** files committed under `assets/`, suitable for sharing;
- **generated assets:** waveform caches, previews, renders, and stems under
  ignored cache/export directories.

For portability, Gloopy should offer a "consolidate composition" command that
copies referenced samples/SFZ files into `assets/` and rewrites paths relative to
the project. This should be explicit because full SFZ libraries can be very
large.

## Git Workflow

The directory format should make these operations natural:

- commit arrangement edits separately from sound-design edits;
- branch a song and merge changed clips without conflicts in mixer settings;
- review a collaborator's PR and see exactly which notes or automation points
  changed;
- tag released versions;
- store scripts that generated or transformed the composition;
- attach docs such as lyrics, cue notes, licensing notes, or render checklists.

A generated `.gitignore` should start with:

```gitignore
.gloopy-cache/
exports/
*.wav.tmp
plugins/scans/
```

Projects may intentionally commit `exports/mix.wav` or stems for release; the
ignore file is only a default.

## Loader And Saver Behavior

Initial implementation should be conservative:

1. Detect whether the input path is a file, zip, directory, or repo.
2. Locate `gloopy.toml`.
3. Parse manifests and text files into the existing runtime model.
4. Resolve sample/SFZ paths with the same policy used by `.gloopy` projects.
5. Save only dirty files, preserving file boundaries, comments, and hand
   grouping.
6. Keep `.gloopy` XML import/export as a compatibility path.

Saver stability matters. Gloopy should not rewrite the whole repo because a fader
moved. Formatting should be deterministic, but restricted to the files whose
model objects changed.

## Migration Plan

Phase 1:

- Add a writer that exports the current runtime model to a composition directory.
- Add a read-only loader for that directory format.
- Keep `.gloopy` as the default save format until round-trip tests are strong.

Phase 2:

- Make directory compositions first-class in File Open/Save.
- Add zip read support.
- Add dirty-file tracking so saves rewrite only changed files.
- Add tests for `.gloopy -> directory -> runtime -> directory`.

Phase 3:

- Add "consolidate composition" for assets.
- Add optional zip write/export.
- Add plugin-state sidecar files and plugin missing-state diagnostics.
