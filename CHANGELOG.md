<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Changelog

All notable changes to Gloopy are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and Gloopy aims to follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) once it reaches its first
tagged release.

> **Experimental.** `0.1.0` is Gloopy's first release; it remains under active
> development. Interfaces, the project format, and behaviour may still change without a
> deprecation window until the API stabilises.

## [Unreleased]

### Added
- **Linux desktop integration.** The RPM/DEB packages now ship a `.desktop` entry, an
  application icon, and AppStream metadata, so Gloopy appears in the application
  menu/launcher and in software centres after install — not just on the `PATH`.
- **Keyboard shortcuts.** `Delete`/`Backspace` removes the selected arrangement clip (or
  clears the selected session slot), and `?` opens a keyboard-shortcut help overlay.

### Fixed
- The About dialog and status bar now show the actual release version instead of a
  hardcoded `0.1.0`.
- Long, message-thread-bound loads (templates, opening a project, adding a plugin,
  importing audio) now reliably show the "Loading…" busy indicator instead of briefly
  freezing with no feedback.

## [0.1.1] - 2026-07-30

A packaging follow-up to 0.1.0: the `dnf` repository now publishes correctly, and the
release notes and Windows startup diagnostics are improved.

### Added
- Windows startup diagnostics: `--safe-mode` skips audio, MIDI, OSC, and gRPC startup,
  while `--force-gdi` forces the Windows GDI/software renderer and logs the available
  JUCE renderers. These switches make Wine/Windows black-window startup failures easier
  to isolate.
- The GitHub Release notes are now generated from this changelog — an announcement line
  plus the release's entry — instead of being empty.

### Fixed
- The `dnf` package repository now publishes to GitHub Pages. In 0.1.0 the repository
  jobs deployed as one atomic push that included the oversized `.deb`, which GitHub
  rejected; dropping the apt repository removes that blocker so the `dnf` repo publishes
  on its own.

## [0.1.0] - 2026-07-30

This is the first public release of Gloopy — a scriptable, composition-as-repo DAW
that hosts VST3/LV2 plugins, features a bundled Surge XT synth, and is driven end to
end by an OSC + gRPC control API over a diff-friendly, version-controllable project
format.

### Added
- **Windows support.** A native Windows x64 build, distributed as both a portable
  `gloopy-windows-x64.zip` and a `gloopy-setup-x64.exe` installer (per-user, Start-menu
  shortcut, optional "add to PATH", uninstaller). The featured Surge XT synth ships as
  a bundled VST3.
- **Linux packages.** Traditional RPM and DEB packages with an FHS layout, plus a
  `dnf` package repository hosted on GitHub Pages, so `dnf install gloopy` works once a
  release is published. The `.deb` is a direct download (`sudo apt install ./gloopy_*.deb`) —
  there is no apt repository, because the package embeds the ~90 MB Surge plugin and so
  exceeds GitHub's 100 MB file limit, and apt cannot redirect package fetches to a
  Release asset the way the `dnf` metadata does.
- **MCP server.** `gloopy mcp` exposes Gloopy over the Model Context Protocol, so an
  AI agent (Claude or any MCP client) can inspect and drive a project through the same
  model the app and scripting API use.
- **The manual.** A documentation site (user guide + control-and-scripting guide)
  published to GitHub Pages, and a project landing page.
- **About dialog.** File → About Gloopy shows the version, copyright, license, bundled
  third-party components, and the written offer of source (AGPL §13).
- Third-party license texts are bundled into every package and the Windows build.
- Supply chain: the RPM package and its repo metadata are GPG-signed, and release
  artifacts ship with an SPDX **SBOM** and **Sigstore** build-provenance attestations
  (verify with `gh attestation verify <file> --repo atgreen/gloopy`).

### Changed
- Packaging moved off CPack to a traditional `rpmbuild`/`dpkg-buildpackage` flow that
  ships only Gloopy's own files (no vendored headers or build tooling).

### Fixed
- Numerous portability fixes for the first MSVC/Windows build (CRT-runtime and zlib
  link conflicts, header/name collisions, and non-standard-C++ constructs), all scoped
  so the Linux build is unaffected.

[Unreleased]: https://github.com/atgreen/gloopy/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/atgreen/gloopy/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/atgreen/gloopy/releases/tag/v0.1.0
