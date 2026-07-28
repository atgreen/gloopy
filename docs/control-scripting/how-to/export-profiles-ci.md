# Render export profiles from a script or CI

**Goal:** bounce the same thing, the same way, every time — a streaming master, a
stems folder, a specific range — without re-specifying it. Define a named
[export profile](../concepts/model.md#export-profile) once; run it by name from a
script or CI.

## Why a profile instead of a one-off render

`RenderToFile` bounces once with ad-hoc arguments. An **export profile** is a
*saved, named* target that travels with the project — so "render the streaming
master" or "bounce stems" becomes one repeatable call, and CI produces identical
outputs on every commit.

## Define profiles

A profile has a **target** — `mix`, `range` (+ a named range), `track` (+
`track_id`), or `stems` — plus format and tail:

```python
from gloopy import Gloopy

with Gloopy() as g:
    g.define_export_profile("master", target="mix", tail_seconds=2.0)
    g.define_export_profile("stems",  target="stems")
    g.define_export_profile("chorus", target="range", range_name="chorus")
    g.list_export_profiles()     # [{'name': 'master', 'target': 'mix', ...}, ...]
```

Profiles persist with the project (they save into the composition), so they're
defined once and reused.

## Run one

`run_export` renders the named profile and returns the list of files written:

```python
files = g.run_export("stems", out_dir="/tmp/build")
print(files)                     # ['/tmp/build/Kick.wav', '/tmp/build/Snare.wav', ...]
```

## In CI

Load the project, run the profiles, done — a headless, deterministic build step:

```python
from gloopy import Gloopy

with Gloopy() as g:
    g.load_composition("song/")          # the composition folder from the repo
    for name in ("master", "stems"):
        for path in g.run_export(name, out_dir="artifacts"):
            print("wrote", path)
```

Or straight from the shell with `grpcurl`:

```sh
grpcurl -plaintext -proto proto/gloopy.proto -import-path proto \
    -d '{"name": "master", "out_dir": "artifacts"}' \
    127.0.0.1:50051 gloopy.v1.Gloopy/RunExport
```

!!! note "Desktop"
    Export profiles are a **scripting** feature — there's no desktop menu for
    them. From the app, the [File-menu exports](../../user-guide/how-to/export-audio.md)
    (Audio / Loop Region / Stems) cover one-off bounces.

## See also

- [Export profile](../concepts/model.md#export-profile) and
  [Location](../concepts/model.md#location) (named ranges for `target="range"`).
- [Render vs. Export Stems vs. Export Profile](../../user-guide/what-to-use-for-what.md#render-vs-export-stems-vs-export-profile).
