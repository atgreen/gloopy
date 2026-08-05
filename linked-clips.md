# Linked / Pooled Clips

> **Status: Implemented (v1).** "Edit one, all update" clips — aliases / shared copies /
> pooled patterns, in the vocabulary of other DAWs. Grounded in the current model:
> `Source/Clip.h` (`struct Clip`), the note sidecars in `Source/Composition.cpp`, the shared
> step/pattern grid in `Source/StepEditor.h`, and the DrumKit voices in `Source/DrumKit.h`.
>
> **The shipped v1 uses the "propagate-on-edit" model, not the `shared_ptr<PatternSource>`
> rewrite sketched below.** A clip carries a `linkId` (`Clip::linkId`); clips sharing a
> non-empty id share a pattern, and editing the notes of any one fans out to the others in
> `MainComponent::syncLinkedClips`. This keeps `notes` by-value — so the real-time render path
> and every read site are untouched, it round-trips through the whole-project undo/serializers
> with a single scalar property, and it avoids turning every clip-copy into an accidental link.
> Editing propagates from the piano roll / step grid and the note-transform tools; shape ops
> (split/crop/stretch/consolidate) detach a clip. Create with **Duplicate Linked**
> (`Cmd/Ctrl+Shift+D` / clip menu / `apiDuplicateClipLinked`); detach with **Make Unique**
> (`apiMakeClipUnique`). Serialized as a `link` key in both formats. A future pass can add the
> `patterns/` file-dedup below; the sections that follow are the original design sketch.

## Why

Today a clip **owns** its content (`Clip.h:13` — *"a clip OWNS its content"*). Copy a clip and
you get an independent duplicate; a later edit to one leaves the others untouched. Many DAWs
offer the opposite as a first-class tool — FL Studio *pattern clips*, Cubase *shared copies*,
Logic *aliases*, REAPER *pooled MIDI* — where several placements reference one piece of content
and an edit propagates to all. Ableton Live and Bitwig deliberately *don't* have this, so for a
session-view-inspired DAW it's a point of **differentiation**, not imitation.

It also pays for itself immediately: the bundled demos already contain repeated sections (a
verse drum groove tiled across many bars) stored as many clips with *identical but separate*
patterns. With pooling those become one shared pattern edited in one place, and the composition
files shrink.

## Core idea: separate the *content* from the *placement*

A clip splits into two layers. Linking shares the first and keeps the second per-instance:

| Layer | Fields | Linked? |
|-------|--------|---------|
| **Pattern (shared content)** | `notes`, `contentLenBeats` | **Shared** — the linked thing |
| **Placement (per-instance)** | `startBeat`, `lengthBeats`, `looped`, `transpose`, `velocityScale`, `muted`, `colour`, audio fades/gain | **Independent** per clip |

Keeping `transpose` and `velocityScale` per-instance is the whole point of an alias: the same
riff at bar 8 and bar 24, one of them a fifth up, both updating when you fix a wrong note.
Keeping `lengthBeats`/`looped` per-instance lets the same 1-bar pattern play for 8 bars in the
verse and 2 bars in the intro.

## Runtime model (C++)

Introduce a shared **`PatternSource`** and have the clip hold it by `shared_ptr` — exactly how
`Clip::audio` is already `std::shared_ptr<AudioBuffer>` today (`Clip.h:32`, *"shared so copies
are cheap"*). MIDI content simply gets the same treatment audio already has.

```cpp
struct PatternSource            // one pooled pattern; many clips may share it
{
    juce::String            id;             // stable slug, e.g. "verse-a"
    juce::String            name;           // optional label for the pattern library
    std::vector<Note>       notes;          // THE shared content
    gloopy::time::BeatRatio contentLenBeats { 4, 1 };  // intrinsic loop window
    juce::Colour            colour;         // optional; clips may still override per-instance
};

// In struct Clip: replace the owned `std::vector<Note> notes;` with
std::shared_ptr<PatternSource> pattern;     // never null for a MIDI clip
```

- **Owned (unlinked) clip** = a `PatternSource` with `use_count() == 1` (a private, unnamed
  source). Nothing about playback changes; this is the default and the migration target for
  every existing clip.
- **Linked clips** = two or more clips holding the *same* `shared_ptr` (`use_count() > 1`).
- Rendering, the arp expansion (`Clip.arpNotes`), and the step/piano editors all read
  `clip.pattern->notes` instead of `clip.notes`. The step grid's write-back
  (`StepEditor::onNotesChanged`) mutates the shared `PatternSource`, so every referencing clip
  re-renders from the same edit — no fan-out bookkeeping.

**Undo** treats a note edit as one operation on the `PatternSource`; undoing restores the shared
content, so all instances revert together (consistent with "they are the same pattern").

## Operations

- **Make Unique** — clone this clip's `PatternSource` into a fresh private one and repoint only
  this clip. The others keep sharing the original; this clip is now independent. (Cubase's
  *Convert to Real Copy*, FL's *Make unique*, Logic's *alias → real copy*.)
- **Paste Linked / Duplicate Linked** — the inverse of a normal paste: the new clip shares the
  source `PatternSource` instead of copying it. Offer both (e.g. `Cmd/Ctrl+D` duplicates
  independent as today; `Cmd/Ctrl+Shift+D` duplicates linked), so the existing
  `duplicateSelectedClip()` path stays the default.
- **Link to…** — point the selected clip(s) at an existing pattern (chosen from the pattern
  library or by clicking a reference clip), replacing their content.
- All three are exposed on the clip context menu, as keyboard shortcuts, and over the control
  API / MCP surface (`link_clip`, `unlink_clip` verbs) so headless/scripted composition can use
  them too.

## Serialization (composition-as-text)

Pooled patterns become **named, reusable assets** in the composition repo — which is exactly the
spirit of the format. Two additive changes, fully backward-compatible:

1. A top-level **`patterns/`** directory with one `patterns/<id>.notes` per shared pattern (the
   same TSV note format the per-clip sidecars already use), plus a **`patterns.toml`** index:

   ```toml
   [[patterns]]
   id = "verse-a"
   name = "Verse groove"
   content_length = 4
   colour = -2070982
   notes = "patterns/verse-a.notes"
   ```

   Registered in `gloopy.toml` like the other indices: `[patterns] file = "patterns.toml"`.

2. A clip references a shared pattern with a new **`pattern`** key instead of the private
   `notes` sidecar:

   ```toml
   [[clips]]
   id = "drums-3"
   start = 32
   length = 96
   content_length = 4      # may be omitted; inherited from the pattern
   looped = true
   transpose = 0           # per-instance, as today
   pattern = "verse-a"     # <-- shared; NO private notes file written
   ```

The loader **pools by id**: every clip with the same `pattern` gets the same `PatternSource`
in memory. A clip that still writes `notes = "clips/…/x.notes"` is simply an owned clip — so
**existing compositions load unchanged**, and a clip is linked *iff* it uses `pattern`. The
content-addressed writer (`Composition.cpp`, dirty-tracking) already prunes files with no
referent, so a shared pattern is written **once** and orphaned patterns are cleaned on save.

*Make Unique on disk* = copy `patterns/<id>.notes` to a private `clips/<track>/<clip>.notes`
and swap the clip's `pattern` key back to `notes`.

## UI

- Linked clips carry a small **link badge** (a chain glyph, à la Cubase's `=`), and optionally a
  shared accent so instances of one pattern read together at a glance.
- The step/pattern-editor header shows **"editing 'verse-a' — used by N clips"**, making the
  propagation obvious before you type.
- A later **Pattern Library** panel (browser sidebar) can list patterns for drag-to-place and
  reuse — this is where pooling meets the [step/pattern-editor] and [session-view] epics.

## Edge cases & decisions

- **Delete a clip** never deletes a pattern that other clips still use; a pattern with zero
  referents is pruned on save (or kept if a Pattern Library is retaining it).
- **Cross-track linking** is allowed (FL permits it), but a pattern's notes are pitch/pad
  addressed — linking a drum pattern onto a melodic track plays different sounds. Warn, don't
  forbid.
- **Audio clips** already share their buffer by `shared_ptr`, and per-clip audio edits
  (gain, fades) are non-destructive placement attributes — so "linked audio" adds little;
  scope v1 to **MIDI/pattern clips**.
- **Script clips** are *already* a form of pooling: two clips with the same
  `scriptSource`/`scriptGenerator` + `scriptSeed` regenerate identical notes (`Clip.h:45-58`).
  Linked note patterns are the hand-edited analog; the two models sit side by side.

## Suggested implementation order

1. **Runtime** — add `PatternSource`; change `Clip.notes` → `shared_ptr<PatternSource> pattern`;
   route all note access (render, arp, editors) through it. Every clip gets a private source;
   behaviour and files are unchanged. (Pure refactor, unit-testable.)
2. **Format** — `patterns/` + `patterns.toml` + the `pattern` clip key; loader pools by id,
   writer emits each shared pattern once. Round-trip test.
3. **Operations** — Make Unique, Duplicate Linked, Link to…; context menu, shortcuts, and
   `link_clip`/`unlink_clip` control-API verbs.
4. **UI** — link badge, pattern-editor "used by N clips" header, optional Pattern Library panel.
5. **Dogfood** — convert the demos' repeated sections to linked patterns (shrinks the files and
   proves the model end to end).
