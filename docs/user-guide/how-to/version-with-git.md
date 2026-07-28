# Version your project with git

A Gloopy composition saved as a **folder** is plain, readable text — one small file
per track, clip, and setting. That makes it a natural fit for **git**, the same version
control developers use for code: every save is a diff you can inspect, and you can keep
a full history of how a track evolved.

This guide covers starting a version-controlled project and checking its status. Making
commits, branching alternate arrangements, tagging finished mixes, and pushing to a
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
