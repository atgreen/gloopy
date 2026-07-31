# Write a script-clip generator

A **script clip** produces its notes from code. Gloopy runs a language **kernel** that
holds your generator; you write the generator, Gloopy runs it and receives the notes.
Generators can be written in **Common Lisp** (a bundled SBCL kernel) or **Python**.

For the desktop workflow, see [Generate a clip's notes from
code](../../user-guide/how-to/generate-notes-from-code.md); this page is the scripting
detail.

## How it runs

The kernel is a small program Gloopy launches on demand. Gloopy hands it the clip's
**context** (tempo, clip length, key, a seed, an optional source file) and the kernel
**posts the generated notes back** to Gloopy — the kernel is a *client* of Gloopy, the
same way the [Python](../tutorials/python-quickstart.md) and
[Common Lisp](../tutorials/lisp-quickstart.md) clients are. So a generator in any language
only needs to talk to Gloopy as a client.

## The generator contract

Your source file defines one generator and registers it with `set-generator`
(`set_generator` in Python). The generator takes a **context** and returns a list of
**notes**:

- `note(pitch, start, length[, velocity])` — a note; `start`/`length` in beats within the
  clip, `velocity` 0..1.
- The context provides `clip-len-beats`, `key-root` (0–11, or −1 for unset), and `seed`.

=== "Common Lisp"

    ```lisp
    (in-package :gloopy-kernel)
    (set-generator
     (lambda (ctx)
       (let ((beats (max 1 (floor (gloopy.pb::clip-len-beats ctx))))
             (root  (+ 60 (max 0 (gloopy.pb::key-root ctx)))))
         (loop for b below beats collect (note (+ root (* 2 b)) b 0.9)))))
    ```

=== "Python"

    ```python
    def gen(ctx):
        beats = max(1, int(ctx.clip_len_beats))
        root  = 60 + max(0, ctx.key_root)
        return [note(root + 2 * b, b, 0.9) for b in range(beats)]
    set_generator(gen)
    ```

## Determinism and caching

Each generation is seeded from the clip's seed, so it is reproducible: same code + same
seed → same notes. Gloopy caches the generated notes in the project, so the clip renders
and plays with **no runtime installed** — the runtime is only needed to *re-generate*.

Gloopy keeps a **resident "warm" kernel** running (auto-started when SBCL is installed):
the proto compiles once, then every generate is instant. That same warm image also hosts
a Slynk server, so you can attach Emacs and redefine generators live — see
[Develop generators live in Emacs](develop-in-emacs.md).

## Over the control API

The same operations are RPCs on the `gloopy.v1.Gloopy` service, so scripts and agents can
drive them:

| RPC | Does |
|-----|------|
| `RegenerateClip(track_id, index, script, lang, seed)` | Generate and materialise a clip's notes |
| `StartDriver(...)` / `StopDriver()` | Live-drive a clip during playback (ephemeral) |
| `StartKernelRepl()` | Return the warm kernel's Slynk port, to attach Sly/SLIME (see [Develop generators live in Emacs](develop-in-emacs.md)) |

## Requirements

- **Common Lisp** — SBCL, with `ag-grpc` available (`ocicl install ag-grpc`).
- **Python** — `grpcio` and `grpcio-tools` (a virtualenv is the clean way to install them).
