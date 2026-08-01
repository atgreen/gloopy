# Live script clips from a Jupyter notebook (Python)

Gloopy generates Python script clips **headlessly** — it auto-launches its own Python kernel,
so a `lang="python"` clip generates with nothing extra running (just as it auto-launches SBCL
for Lisp). You only bring a notebook when you want to *author* generators.

The interesting part is **attaching a notebook** as the live generator source: redefine a
generator in a cell and a [**Live** clip](../../user-guide/how-to/generate-notes-from-code.md)
picks it up on the next loop, no restart. It's the Python analogue of connecting Emacs to the
Lisp kernel: your process *is* the live image.

Attaching **takes over** from the auto-launched kernel — exactly one Python kernel serves at a
time, so the two never race. Detach (or close the notebook) and Gloopy resumes generating on
its own from each clip's cached notes / its headless kernel.

A ready-to-run notebook ships at `python/notebooks/live-clips.ipynb`. (Gloopy also surfaces the
connect details under **File → Open Python Notebook…**.)

## Prerequisites

- Gloopy running (it listens on `127.0.0.1:50051`).
- `pip install grpcio` and the `gloopy` client package (`python/`).

## Attach

```python
import gloopy
from gloopy import Gloopy

g = Gloopy()             # the control API — add tracks, clips, transport, …
k = gloopy.attach()      # attach THIS process as the live Python kernel
```

`attach()` starts a background thread that long-polls Gloopy for **Python** generate jobs.
Because jobs are routed by language, this coexists with Gloopy's own SBCL kernel — Lisp clips
still go to Lisp, Python clips come here.

## Define a generator

A generator takes a context (`clip_len_beats`, `seed`, `key_root`, `tempo_bpm`) and returns
notes. Register it with the `@k.generator` decorator:

```python
@k.generator
def bassline(ctx):
    root = 36 + (ctx.key_root if ctx.key_root >= 0 else 0)
    steps = [0, 0, 7, 5]
    return [gloopy.note(root + steps[b % 4], b, 0.9) for b in range(int(ctx.clip_len_beats))]
```

The module-level `@gloopy.generator` works too (it attaches on first use), so you needn't keep
the `k` handle around: `import gloopy` then `@gloopy.generator`. Name a generator —
`@gloopy.generator("bass")` — to have a clip reference it by name.

## Generate a clip

```python
t = g.add_synth_track(name="Bass", wave="SAW")
g.add_clip(t, start_beat=0, length_beats=4)
g.regenerate_clip(t, 0, lang="python", seed=1)   # runs your generator, materialises the notes
g.get_clip_notes(t, 0)
```

## Go live

Mark the clip **Live** and loop it. Now edit the generator cell and re-run it — the clip
updates on the next pass **while it plays**:

```python
g.set_clip_script_live(t, 0, True)
g.set_loop_to_clip(t, 0)
g.play()
# …edit @k.generator in a cell, re-run it, hear the change next loop…
g.stop()
```

Errors in your generator are caught and reported back to Gloopy (the clip keeps its last good
notes) rather than killing the bridge — check `k.last_error`. When you're done, `k.detach()`;
Gloopy then falls back to each clip's cached notes.

## How it works

- By default Gloopy runs its **own** headless Python kernel, so clips generate with no notebook
  at all. When you `attach()`, your process refreshes a heartbeat file and Gloopy **stands its
  headless kernel down** — one Python kernel serves at a time, so the two never race for a job.
  Detach (or close the notebook) and the heartbeat goes stale; Gloopy relaunches its headless
  kernel on the next generate.
- `gloopy.attach()` long-polls `KernelPoll(lang="python")`; on a job it builds the context,
  calls your registered generator, and posts notes via `KernelSubmit`.
- The generator lives in your notebook's namespace, so redefining it (re-running the cell) is
  immediately live — Gloopy re-runs it every generate, and **Live** clips regenerate about a
  bar ahead of playback.
- The notebook owns the kernel's lifecycle. Restart the notebook kernel and the bridge drops;
  Gloopy keeps playing each clip's cached notes until you re-attach.

!!! note "Reproducibility"
    A notebook-defined generator isn't a file Gloopy can reload on its own, so save the
    notebook as the clip's "source of truth". The materialised notes are cached in the project
    and play without the notebook; you only need it to *re-generate*.
