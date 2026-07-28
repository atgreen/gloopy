# Version your project with git

A Gloopy composition saved as a **folder** is plain, readable text — one small file
per track, clip, and setting. That makes it a natural fit for **git**, the same version
control developers use for code: every save is a diff you can inspect, and you can keep
a full history of how a track evolved.

This guide covers starting a version-controlled project, checking its status, and making
commits. Branching alternate arrangements, tagging finished mixes, and pushing to a
remote are being added on top of this foundation.

## Before you start

- Install **git** on your system. Gloopy uses your existing git — including your name,
  email, and any keys or logins you already have set up.

## Start a version-controlled project

Use **File → New Git Project…**, choose a folder, and Gloopy saves your song there as a
**Composition folder** and turns it into a git repository in one step — ready for its
first commit. (The Composition folder format is where version control shines: each change
touches only the files it needs to, so history stays clean and readable.)

Already working in a Composition folder? **File → Enable Git** turns the current folder
into a repository in place.

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
