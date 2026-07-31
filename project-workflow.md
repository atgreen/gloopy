<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Project workflow — design note (draft for review)

Status: **draft / request for comment.** This proposes making a Gloopy project a
self-contained, reproducible repo, and — the guiding constraint — one that feels like an
*idiomatic* project to the people who'll write the generators: **Python/Jupyter** developers
and **Common Lisp** developers. Nothing here is built yet.

## Goal

Today a Gloopy composition is *almost* self-contained: renders, exports, recorded takes and
plugin state all live in the composition directory. Two things escape it:

1. **Clip scripts** live in a global dir (`~/.local/share/Gloopy/scripts/clip-<uuid>.lisp`)
   and clips store an **absolute** path. The composition even tries to keep the script
   (`ctx.keep(src)`), but the file is outside the dir, so a clone loses it.
2. **Kernel dependencies** (ocicl `ag-grpc`, a Python env) are user-global — no per-project
   pinning, classic "works on my machine."

The aim: **a Gloopy project is a git repo that carries the music, the generative code, and
the code's pinned dependencies — reproducible from a single `git clone`.**

**Core invariant (the north star everything else rides on):** *a cloned composition folder
must contain every authored source file needed to regenerate its clips.* The ambitious
"native project idioms" work below is valuable, but secondary to this; slices 1–3 satisfy the
invariant on their own.

## Guiding principle: embrace native project idioms, don't reinvent them

The load-bearing design decision. A Gloopy project should *also be*:

- a normal **ASDF/ocicl** Common Lisp project, and
- a normal **pyproject/venv** Python package,

so that a Lisp dev sees a project they can `(asdf:load-system …)` and `M-x sly` into, and a
Python dev sees a package they can `pip install -e .` and open in Jupyter. Gloopy
**orchestrates** the standard tools; it does not hide or replace them. A developer who
prefers their own workflow can drive the project entirely from the CLI, and Gloopy uses the
result.

## Project layout

```
myproject/                     # a git repo (canonical form; the .gloopy zip is export-only)
  gloopy.toml  tracks/  clips/<t>/<c>.notes     # existing composition format
  README.md                                      # "reproduce: gloopy myproject/"
  .gitignore                                     # installed envs, caches, raw takes, exports

  # --- Common Lisp side (idiomatic ASDF + ocicl) ---
  myproject.asd                                  # an ASDF system for the generators
  src/*.lisp                                     # generators as a real package/system
  systems.csv                                    # ocicl lockfile (pinned deps) — COMMITTED
  systems/                                        # ocicl-installed deps — gitignored

  # --- Python side (idiomatic pyproject + venv) ---
  pyproject.toml                                 # PEP 621; deps incl. gloopy-client
  src/myproject/generators.py                    # generators as an importable module
  requirements.lock  (or uv.lock)                # pinned deps — COMMITTED
  .venv/                                          # installed env — gitignored

  notebooks/*.ipynb                              # optional
```

Both language sides are **lazy**: a Lisp-only project never grows a `pyproject.toml`, and
vice-versa. The `.asd`/`pyproject.toml` appear when the first clip in that language is created.

### Generators as systems/modules, not `exec`'d files

Idiomatic, and a change from today. Instead of `exec(open(path))` / `(load path)` of a bare
file:

- **Lisp**: generators are functions in the project's package, loaded as an ASDF system. A
  clip references a generator by *name* (a symbol). Redefining it in Sly is naturally live —
  which is exactly the image model CL devs expect.
- **Python**: generators are functions in an importable module (`myproject.generators`). A
  clip references one by dotted name. In a notebook you `import` it (or `%autoreload`), and
  redefining the cell/function is live.

This makes generators testable and importable on their own — a CL dev can `(load-system)` and
call them at the REPL; a Python dev can `pytest` them — with no Gloopy running.

**But bare-file script clips stay first-class, permanently** — not merely a migration
waypoint. A quick per-clip source file is the low-ceremony path; named generators are for
those who want the idiomatic system/module structure. Both are valid and supported side by
side (see the metadata contract next).

## Clip metadata contract

The piece to get right. A clip references its generator one of two ways, kept as **distinct
first-class fields** rather than overloading a single `script` string:

| Field         | File-based clip                | Named-generator clip                                   |
|---------------|--------------------------------|--------------------------------------------------------|
| `script`      | `scripts/arp.lisp` (relative)  | —                                                      |
| `generator`   | —                              | `myproject.generators:arp` (py) · `myproject:arp` (lisp) |
| `lang`        | `common-lisp` / `python`       | same                                                   |
| `system`      | —                              | ASDF system / import root to load (Lisp; optional if derivable) |
| `seed`        | int                            | int                                                    |

(The existing `script_lang` / `script_seed` keys are retained as aliases, so old projects
parse unchanged.)

Resolution at generate time:

1. `generator` present → load the project's system/package, resolve the name, call it.
2. else `script` present → resolve the relative path against the project dir, load the file
   (which registers a generator — today's mechanism), call it.

Both carry `seed`; both cache their notes in `clips/<t>/<c>.notes`, so a clip plays/renders
with **no runtime installed** — the env is only needed to *re-generate*.

### Kernel-protocol impact

Today the job spec carries only `source` (a file path). Named generators add a `generator`
(and, for Lisp, a `system`) field to `KernelJobSpec`; the kernel loads the system/package and
resolves the name instead of `load`-ing a bare file. That is slice 5 — the file path keeps
working until then and after.

### Migration of `script = "/abs/path"`

The un-cloneable case (absolute path into global app data). Migration keeps the clip
**file-based** — no forced rewrite to a named generator: copy the file into `scripts/`,
rewrite `script` to the relative path, save. A separate, optional "promote to a named
generator" refactor can later move the code into the system/module and switch to
`generator = …`. Nothing forces that move.

## Dependency + environment model

**Reproducibility contract: commit the lockfiles, gitignore the installed envs.** A `.venv`
or an ocicl `systems/` tree is large and platform-specific; committing it is wrong. The
lockfile (`systems.csv`, `uv.lock`/`requirements.lock`) is what makes `clone` reproducible.

**Who reconstructs the env from the lockfile?** The project is a *standard* project, so the
native tools already do it:

- Lisp: `ocicl install` in the project dir (ocicl is already the kernel's dep manager).
- Python: `uv sync` (or `python -m venv && pip install -r`). **`uv` is worth standardising
  on** — it makes env creation seconds, not minutes, which defuses the main objection to
  Gloopy doing it automatically.

Gloopy's role is a **convenience wrapper, not a gate**: on opening a project whose lockfile
is present but whose env is missing, Gloopy offers (consent-gated, with visible progress) to
run the standard command for you — "Set up this project's environment?" → `uv sync` /
`ocicl install`. Decline, and the project is still a normal project you set up by hand. This
keeps the "clone and it plays" promise without making Gloopy a package manager or hiding the
real tooling.

**An env is never required to *play* — only to *re-generate*.** Because generated notes are
cached in the composition, a fresh clone plays and renders with no venv/ocicl at all. So `uv`
is standardised on for the *authoring* loop but is not a playback dependency; env setup is a
later slice, not a blocker. Standardise on `uv` for speed and lockfiles, but keep a stdlib
`venv`+`pip` fallback so it isn't a hard prerequisite.

### The Jupyter loop

If the project owns a `.venv`, Gloopy registers a Jupyter **kernelspec** (`gloopy:myproject`)
pointing at it, so the notebook's kernel *is* the project env — the notebook and Gloopy agree
on dependencies by construction. `gloopy.attach()` from that kernel becomes the live Python
generator source (already built); redefining a cell is picked up on the next generate / Live
clip.

### The Sly loop

The kernel already hosts Slynk and `gloopy.el` connects Sly. With generators as an ASDF
system, the flow is the plain CL one: connect Sly, `C-c C-k` the system, redefine a generator,
regenerate. No Gloopy-specific ceremony.

## "Always have a project"

Gloopy always operates inside a project dir. An unsaved session uses a scratch dir
(`~/.cache/gloopy/scratch/<uuid>`); **Save As** copies the composition + `src/`/`scripts/`
into the chosen location, leaving scratch disposable.

`git init` is **explicit and scoped**: it happens only for **New Project Folder** / Save-As to
a real location — with UI copy that says plainly "Initialize a git repository here" and an
opt-out — and **never** for ephemeral scratch sessions (they're throwaway; a repo there is
noise). This keeps the "song as repo" default without surprising anyone or littering `.git`
dirs in temp.

## Migration

Existing projects have absolute/global scripts. On open, if a clip's `scriptSource` is
absolute and outside the project, offer **once**: "Import scripts into this project?" → copy
in, rewrite to a relative path, save. Non-destructive; absolute paths keep resolving for
anything un-migrated.

## Implementation order

Sliced so the invariant lands first and the ambitious idioms ride on top. **Slices 1–3 are
the whole cloneability win and are small; 4–6 are the "native project" layer.**

1. **Create script files inside the project dir**, not global app data
   (`scriptsDir()` → `<project>/scripts`).
2. **Relative paths on save/load** — rewrite in-project absolute paths to relative; resolve
   relative against the project dir. *(1–2 satisfy the core invariant.)*
3. **Migration prompt** — import global/external scripts into the project (copy + rewrite
   relative), non-destructive.
4. **Project scaffolding**, lazy per language — `.asd` / `pyproject.toml`, `.gitignore`,
   README, only when the first clip in that language appears.
5. **Generator-by-name** — the `generator` (+ `system`) metadata and kernel-side resolution.
6. **Env setup helpers** — consent-gated `uv sync` / `ocicl install`, only after the layout
   is stable.

## Decided (from review)

- Composition dir is the canonical repo; `.gloopy` is export/share. **Yes.**
- Lazy language scaffolds (no Python files in a Lisp-only song, and vice-versa). **Yes.**
- Generators as ASDF systems / Python modules — right long-term model, **with a permanent
  compatibility layer for bare-file script clips** (not just during migration).
- Standardise on `uv`, but it is **not required for playback** (cached notes) and not a hard
  prerequisite (stdlib `venv`+`pip` fallback). Staged as slice 6.
- `git init` only on **New Project Folder** / real Save-As, explicit UI copy, opt-out — never
  on scratch sessions.
- Generator references are **first-class metadata** (`generator`/`system`), not overloaded
  into `script`.

## Still open

- **Named-generator syntax**, esp. Lisp: `package:symbol` vs an explicit `system` + symbol,
  and how `system` is derived when omitted.
- **Python version pinning** — deferred; `uv` can pin later. v1 uses ambient `python3`.

## Non-goals (for this pass)

- Replacing ocicl/uv/pip with anything bespoke.
- Committing installed environments.
- Changing the on-disk composition format itself (this rides alongside it).
