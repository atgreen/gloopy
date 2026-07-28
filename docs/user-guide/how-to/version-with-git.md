# Version your project with git

A Gloopy composition saved as a **folder** is plain, readable text — one small file
per track, clip, and setting. That makes it a natural fit for **git**, the same version
control developers use for code: every save is a diff you can inspect, and you can keep
a full history of how a track evolved.

This guide covers the whole workflow: starting a version-controlled project, checking its
status, reviewing changes, committing, branching alternate arrangements, tagging finished
mixes, auditioning older versions, undoing at the repo level, sharing with a remote, and
resolving merge conflicts — all from Gloopy's File menu, over the same git you already use.

## Before you start

- Install **git** on your system. Gloopy uses your existing git — including your name,
  email, and any keys or logins you already have set up.

## The status bar

The strip along the bottom of the window is always in view and tells you, at a glance:

- **Which project** you're in — the full path of the composition folder (or `.gloopy`
  file), with your home directory shown as `~`. An **untitled** project that has never
  been saved shows *Untitled project*.
- **Unsaved edits** — a coloured dot and an *edited* tag appear the moment you change
  anything; both clear when you save. This is in-memory state: it says the project on
  screen differs from the last save.
- **Git state** — once the project is a repository, the bar shows the current **branch**
  and how many files are **uncommitted** (or *clean* when everything is committed), plus
  *up N* / *down N* when a remote is ahead or behind. This is on-disk state: it says the
  saved files differ from the last commit. (The two are independent — you can have saved
  work that isn't yet committed.)
- The **Gloopy version**, on the right.

Everything here is also available to scripts and agents via the `GetProjectStatus` call.

## Start a version-controlled project

**Every Composition folder is a git repository from its very first save.** Whenever you
save your song as a folder — **File → Save As Composition Folder**, or any save of a
project already in folder form — Gloopy initialises git in that folder if it isn't a
repository yet, so it's ready for its first commit with nothing extra to do. (The
Composition folder format is where version control shines: each change touches only the
files it needs to, so history stays clean and readable.)

**File → New Git Project…** does the same thing up front — pick a folder and Gloopy saves
and initialises it in one step — and **File → Enable Git** initialises a folder you're
already working in. With auto-init on save, both are now just shortcuts to a repository
you'd get anyway; the single-file `.gloopy` **archive** is the one form that isn't a repo
(it's a zip for sharing, not for versioning).

## Check the status

Open **File → Source Control**. Gloopy inspects your composition folder and shows:

- whether git is available, and its version;
- the **branch** you're on (and whether you're on a detached snapshot);
- how far **ahead or behind** a remote you are, once one is set up;
- the list of **changed files** since your last commit — the tracks, clips, or settings
  you've touched.

A clean working tree means everything is committed; a list of changes is your cue to
make a commit and capture the current state.

## Save a snapshot with a commit

Use **File → Commit…**. Gloopy saves your song, shows you exactly which files changed —
the tracks, clips, and settings you touched — and asks for a short message describing the
change. **Commit** captures that snapshot; **Amend** folds it into your previous commit
instead (handy for a quick fix-up). Commits use your own git identity, so history is
attributed to you.

Commit whenever you reach a state worth returning to — a finished section, a mix you like,
before a risky experiment. Each commit is a point you can compare against or come back to.

## Review the history

Open **File → History** to see the list of commits, newest first — each with its short
identifier, the message you wrote, and who made it when. It's the timeline of how your
song evolved, and the reference for the version-navigation features coming next.

## Try an idea on a branch

A **branch** is a parallel version of your song — perfect for an alternate arrangement or
a remix you want to explore without disturbing the main version. Open **File → Branches**
to see your branches (the current one is ticked) and:

- **New branch…** — start a fresh line of work from where you are, and switch to it.
- click a branch — **switch** to it (commit your changes first; Gloopy reloads the song
  as it was on that branch).
- **Merge into <current>** — fold another branch's changes into the one you're on.
- **Delete branch** / **Rename current branch…** — housekeeping.

Work on a branch, commit as you go, then either merge it back into your main version or
leave it as an alternate take you can return to any time.

## Mark a milestone with a tag

A **tag** is a memorable name for a specific version — the demo you sent, the master you
delivered, the album cut. Open **File → Tags → Tag this version…**, give it a name (and an
optional note), and that commit is bookmarked forever.

Later, **File → Tags → Check out: <name>** loads the song exactly as it was at that
milestone, so you can listen back or compare. (You'll be viewing a fixed point in history;
use **File → Branches** to return to your working version when you're done.)

## Audition any past version

**File → Open at version…** is the one-stop way to load your song as it was at any point —
pick a **branch**, a **tag**, or any **recent commit** from the list, and Gloopy reloads
the arrangement at exactly that version so you can hear how it sounded. When you open an
older commit or a tag you're just *viewing* history (a fixed snapshot); the same menu has a
**Return to <branch>** entry at the top to jump back to your working version. Commit any
in-progress changes first — Gloopy will remind you if you haven't.

## See what changed

**File → Changes (Diff)…** shows exactly what you've edited since your last commit. Gloopy
leads with a short summary — *which* tracks, clips, and settings changed — then the line-by-line
difference itself. Because a Composition folder is readable text, the difference is genuinely
legible: a filter you nudged shows up as a single changed value, not an opaque blob. It's the
quickest way to review your work before making a commit.

## Undo, shelve, or roll back

**File → Discard / Stash / Reset…** gathers the repo-level "undo" tools:

- **Discard all uncommitted changes** throws away everything you've edited since your last
  commit and returns to that saved state (it asks first — this can't be undone).
- **Stash changes** shelves your current edits so the song returns to its last committed
  state with a clean slate; **Pop latest stash** brings those shelved edits back later.
  Handy for briefly setting an idea aside without committing it.
- **Revert a commit** creates a new commit that undoes an earlier one, leaving the rest of
  your history intact — the safe way to take back a change you already committed.
- **Reset to a commit** moves your project back to a chosen commit. *Soft* and *mixed*
  keep your edits; *hard* discards them (it asks first).

Every one of these reloads the song afterward so what you see matches the new state.

## Share it — remotes, push and pull

To back your project up or collaborate, connect it to a **remote** (a shared copy of the
repository — on GitHub, a company server, or anywhere you can reach). **File → Remotes / Push
/ Pull…** lets you:

- **Add remote…** — register a remote by name and URL (the first is conventionally called
  *origin*). Gloopy uses your existing git setup — the same keys and logins you already have —
  so private servers just work.
- **Push** — send your commits to the remote. This is always something *you* choose to do;
  Gloopy never pushes on its own.
- **Pull** — bring down and merge changes from the remote, then reload the song.
- **Fetch** — check for remote changes without merging them yet.

The menu shows how far **ahead or behind** the remote you are, so you know when it's time to
push or pull. Network steps run in the background with a progress indicator, so a slow upload
never freezes the app.

## Resolve a merge conflict

When you merge a branch (or pull from a remote) and both versions changed the *same* thing,
git can't decide for you — that's a **conflict**. Gloopy makes this painless: **File → Resolve
conflicts…** lists each affected part of your song (a track, a clip, a setting) and, for each,
lets you choose:

- **Accept ours** — keep your current branch's version.
- **Accept theirs** — take the incoming version.
- **Keep both** — merge both sets of changes together.

Once every conflict is settled, **Continue merge** finishes the job and reloads your song; or
**Abort merge** backs out entirely and leaves things as they were. Because a Composition folder
is readable text, conflicts are about real, understandable things — not an opaque "project file
is corrupt" dead end.

## Settings — who you are, and auto-saving to history

**File → Git Settings…** holds two per-project preferences:

- **Commit identity** — the name and email attached to this project's commits. It applies to
  *this project only*, so a personal alias or a band name doesn't leak into your other work.
- **Auto-commit on save** — off by default. Turn it on and Gloopy quietly records a commit
  every time you save, so your history captures every step with zero effort. Leave it off if
  you'd rather commit deliberately at meaningful moments.

## From a script

The same status is available to scripts and agents through the control API, so you can
build version-control steps into your own tools:

```python
from gloopy import Gloopy

with Gloopy() as g:
    info = g.git_status()          # of the open project's folder
    print(info["branch"], "-", len(info["changes"]), "changed file(s)")
```

See the [Python client reference](../../control-scripting/reference/python/index.md)
for `git_available()` and `git_status()`.
