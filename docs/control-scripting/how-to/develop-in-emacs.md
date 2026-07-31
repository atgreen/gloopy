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
   `set-generator` installs it into the running kernel — **it is live immediately**.
3. Back in Gloopy, **Generate from script** on the clip (or call `RegenerateClip` over the
   control API). The kernel runs the generator **as it exists in the image right now**, so
   your just-evaluated redefinition is what runs — no save, no restart.

The kernel loads a clip's source file **once** (the first generate of the session); after
that, the live image is authoritative, exactly as you'd expect hacking at a Lisp REPL. A
clip generates its notes only when you ask it to (step 3) — editing alone doesn't change
what plays until you generate (or **Live-drive from script**).

## Hands-free: mark a clip "Live"

To skip the manual **Generate** entirely, right-click the clip → **Live (auto-generate on
playback)**. A **LIVE** badge appears on the clip. Now, while the transport is rolling,
Gloopy re-runs the clip's generator from the live image **about a bar before the clip
plays** and swaps in the fresh notes — so with a clip looping, every `C-c C-c` is heard on
the next pass, no clicks at all. If a generate ever runs long (e.g. the very first, cold
one), the clip falls back to its cached notes for that pass. Toggle it off to freeze the
clip again. (Over the control API: `SetClipScriptLive(track_id, index, live)`.)

!!! tip "Save to persist"
    Live redefinitions live in the running image, not on disk. **Save the buffer** (`C-x
    C-s`) when you're happy: a *fresh* kernel (Gloopy restart, or reopening the project)
    reloads the generator from the file, which is what keeps the project reproducible. Think
    of it the same way you would any Lisp image — the image is live, the file is the record.

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
