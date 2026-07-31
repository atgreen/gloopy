# Generate a clip's notes from code

**Task:** let a small program write a clip's notes for you — an arpeggio, a
generative pattern, a seeded variation — instead of drawing them by hand. This is a
**script clip**: its notes come from a generator you write in Common Lisp or Python.

!!! note "Experimental"
    Script clips are new and still evolving. You need a language runtime installed —
    **SBCL** for Common Lisp, or **Python** with `grpcio` and `grpcio-tools`.

## Write the generator

1. Add an instrument track and an (empty) MIDI clip on it.
2. **Right-click the clip → Edit script code…**. Gloopy creates a source file for the
   clip and opens it in your editor (from `$VISUAL` / `$EDITOR`).
3. The file starts from a template. Define a generator — a function that returns a list
   of notes:

    ```lisp
    (in-package :gloopy-kernel)
    (set-generator
     (lambda (ctx)
       ;; one note per beat, an ascending run from middle C
       (let ((beats (max 1 (floor (gloopy.pb::clip-len-beats ctx)))))
         (loop for b below beats collect (note (+ 60 b) b 0.9)))))
    ```

    `note` takes `(pitch start length &optional velocity)` — start and length in **beats
    within the clip**. `ctx` gives you the clip length, key, and a per-clip **seed** for
    repeatable randomness.

4. Save the file.

## Generate the notes

**Right-click the clip → Generate from script.** Gloopy runs your generator and fills the
clip with the notes it returns. Edit the code and Generate again to iterate.

The result is **materialised** into the clip — normal notes you can see and edit in the
piano roll, saved with the project. The generated notes are **cached**, so the clip plays
on any machine even without the runtime installed; you only need the runtime to
*re-generate*. Generation is **deterministic**: the same code and the same seed always
produce the same notes.

## Play the script live instead

**Right-click the clip → Live-drive from script** plays the generator's notes **live
during playback** without writing them into the clip — they're ephemeral and re-fetched
each pass. Use this for generative parts you want to stay code, not fixed notes.

## Develop interactively (Common Lisp)

**File → Start Lisp REPL (SWANK)…** launches a warm Lisp image and shows a port. Attach
your editor's Lisp REPL (SLIME/Sly) to `127.0.0.1:<port>` to define and test generators
live before saving them into a clip.

## See also

- [Write a script-clip generator](../../control-scripting/how-to/write-a-script-clip.md) —
  the scripting details, the Python generator, and driving it over the control API.
