<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Security Policy

## Supported versions

Gloopy is **experimental and pre-release** — there is not yet a tagged release. Until
`0.1.0` ships, only the current `main` (and, once they exist, the most recent tagged
release) receives fixes. Older builds are not supported.

| Version | Supported |
|---------|-----------|
| `main` (development) | ✅ |
| tagged releases | the latest one, once releases exist |
| anything older | ❌ |

## Reporting a vulnerability

**Please do not open a public issue for security problems.** Report privately, one of
two ways:

1. **GitHub private advisory (preferred):** on the repository, go to
   **Security → Report a vulnerability** to open a private report.
2. **Email:** <green@moxielogic.com>.

Please include enough to reproduce: affected version or commit, platform, steps, and
the impact you observed.

### What to expect

- An acknowledgement, on a best-effort basis for a solo-maintained experimental
  project — please allow some time.
- We will confirm the issue, work on a fix, and coordinate disclosure with you. Credit
  is given in the release notes unless you prefer to remain anonymous.
- Please give us a reasonable window to release a fix before any public disclosure.

## Scope and context worth knowing

- Gloopy opens a **local control API on startup** — OSC on UDP `9000` and gRPC on
  `127.0.0.1:50051` — with **no authentication**. It is designed for local, trusted use
  (scripts and tools on the same machine). Do **not** expose these ports to an untrusted
  network; anything that can reach them can drive Gloopy and read/write project files.
- Gloopy **hosts third-party VST3/LV2 plugins** and loads project files (a
  composition-as-repo folder or `.gloopy`/zip). Plugins run as native code, and project
  files can reference plugins and audio; treat both as you would any code/content from an
  untrusted source.
- Gloopy vendors third-party components (sfizz, Surge XT, JUCE, and others); their
  licenses and provenance are recorded in
  [`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md).

Fixes and advisories are noted in [`CHANGELOG.md`](CHANGELOG.md).
