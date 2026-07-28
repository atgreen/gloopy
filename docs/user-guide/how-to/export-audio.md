# Export the mix, a range, a track, or stems

**Task:** bounce your song to audio — the whole mix, just a loop, one track, or
every track separately. All exports are **offline and deterministic**: they render
from the [arrangement](../explanation/arrangement-vs-session.md), so you get the
same file every time.

Everything here is in the **File** menu.

## The whole mix

**File → Export Audio (WAV)…** renders the full song to a single WAV (with a short
tail so reverbs ring out). This is the stereo master.

!!! tip "Check your levels first"
    The bounce reflects your mixer exactly — if the master meter clips during
    playback, it'll clip in the file. [Balance the mix](effects-and-mixing.md)
    (and add a master limiter if needed) before exporting.

## Just the loop region

**File → Export Loop Region (WAV)…** bounces only the current **Loop** selection.
It's enabled when Loop is on — set a loop over the section you want and export it.
Handy for rendering a single eight-bar idea.

## One track (a stem)

Right-click a **track header** → **Export track (WAV)…** to bounce that one track
on its own — its clips through its effect chain, in isolation.

## Every track (stems)

**File → Export Stems (WAV)…** asks for a folder and writes **one WAV per
instrument track** into it — the set of stems you'd hand to someone for mixing or
remixing elsewhere.

## Share the whole project instead

If you want to hand over the *editable* project rather than audio, that's
**Export Project (.gloopy)** — a single zip of the composition. It's a different
thing from an audio bounce; see
[Save vs. Export](../what-to-use-for-what.md#save-project-vs-export-project).

!!! note "Repeatable exports (for scripts and CI)"
    A named, saved render target — an **export profile** (`mix` / `range` /
    `track` / `stems`) you run by name — is available through the
    [control API](../../control-scripting/index.md), not a desktop menu. Reach for
    it when you bounce the same thing repeatedly or from CI.

## See also

- [Export profile](../../control-scripting/concepts/model.md#export-profile) — the
  saved-target model.
- [What to use for what](../what-to-use-for-what.md#render-vs-export-stems-vs-export-profile).
