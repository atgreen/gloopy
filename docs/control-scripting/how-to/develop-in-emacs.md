# Develop generators live in Emacs (Sly)

Gloopy keeps a resident Lisp **kernel** running to generate your [script
clips](write-a-script-clip.md). That kernel also hosts a **Slynk** server, so you can
attach Emacs (via [Sly](https://github.com/joaotavora/sly)) to the *very image that makes
your music* — redefine a generator, re-run the clip, and hear the change without a
restart. Once connected, clicking a script clip in Gloopy opens its source right here in
Emacs.

## Prerequisites

- **SBCL** installed. Gloopy starts the warm kernel automatically when it finds `sbcl` on
  the `PATH`; a green **λ Slynk *port*** indicator appears in the status bar once it's
  ready. (Set `GLOOPY_NO_KERNEL=1` to opt out.)
- **Emacs** with **Sly** installed (`M-x package-install RET sly`).

## Set up the package

`gloopy.el` ships in the source tree under `emacs/`. Point Emacs at it and load it:

```elisp
(add-to-list 'load-path "/path/to/gloopy/emacs")
(require 'gloopy)
```

## Connect

With Gloopy running:

```
M-x gloopy-connect
```

This reads the port Gloopy advertised, attaches Sly to the kernel, and turns on
`gloopy-mode`. You now have a REPL on the live kernel — the prelude (`note`,
`set-generator`, …) is already loaded, in the `gloopy-kernel` package.

You can also open the connection instructions from Gloopy itself:
**File → Connect Emacs to Kernel (Slynk)**.

## The live loop

1. Open a clip's source (click the clip in Gloopy, or `C-x C-f` the file).
2. Edit the generator and evaluate it with `C-c C-c` (or `C-c C-k` for the whole buffer).
   `set-generator` installs it into the running kernel.
3. Back in Gloopy, **Generate from script** on the clip (or call `RegenerateClip` over the
   control API). The kernel runs your just-redefined generator and posts the notes back.

Because the kernel is warm and shared, step 2 changes the image step 3 runs against — no
process restart, no proto recompile.

## How the link works

Gloopy writes a small discovery file on startup —
`$XDG_RUNTIME_DIR/gloopy/kernel.json` (falling back to `~/.cache/gloopy/kernel.json`) —
with the Slynk port and the gRPC control port. `gloopy-connect` reads it to find the
kernel.

`gloopy-mode` in turn advertises Emacs back to Gloopy (a presence file, `emacs.json`, next
to the discovery file) so that clicking a script clip routes through `emacsclient` to
`gloopy-open-clip`. If no connected Emacs is advertised, **Edit script code** falls back to
launching a fresh editor.

## Troubleshooting

- **No λ indicator / `gloopy-connect` says no kernel found** — SBCL isn't installed or the
  kernel hasn't warmed up yet. Confirm `sbcl` is on the `PATH` and give it a few seconds on
  first launch (the one-time proto compile).
- **Clicking a clip doesn't open it in Emacs** — make sure you ran `gloopy-connect` (it
  starts the Emacs server `gloopy-mode` needs). It routes to the Emacs that most recently
  connected.
