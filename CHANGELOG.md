<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Changelog

All notable changes to Gloopy are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and Gloopy aims to follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) once it reaches its first
tagged release.

> **Pre-release.** Gloopy is experimental and has not had a tagged release yet.
> Until `0.1.0`, interfaces, the project format, and behaviour may change without a
> deprecation window; the entries below are the running tally of what the first
> release will contain.

## [Unreleased]

### Added
- **Windows support.** A native Windows x64 build, distributed as both a portable
  `gloopy-windows-x64.zip` and a `gloopy-setup-x64.exe` installer (per-user, Start-menu
  shortcut, optional "add to PATH", uninstaller). The featured Surge XT synth ships as
  a bundled VST3.
- **Linux packages and repositories.** Traditional RPM and DEB packages with an FHS
  layout, plus `dnf`/`apt` package repositories hosted on GitHub Pages, so
  `dnf install gloopy` / `apt install gloopy` work once a release is published.
- **MCP server.** `gloopy mcp` exposes Gloopy over the Model Context Protocol, so an
  AI agent (Claude or any MCP client) can inspect and drive a project through the same
  model the app and scripting API use.
- **The manual.** A documentation site (user guide + control-and-scripting guide)
  published to GitHub Pages, and a project landing page.
- Third-party license texts are bundled into every package and the Windows build.

### Changed
- Packaging moved off CPack to a traditional `rpmbuild`/`dpkg-buildpackage` flow that
  ships only Gloopy's own files (no vendored headers or build tooling).

### Fixed
- Numerous portability fixes for the first MSVC/Windows build (CRT-runtime and zlib
  link conflicts, header/name collisions, and non-standard-C++ constructs), all scoped
  so the Linux build is unaffected.

[Unreleased]: https://github.com/atgreen/gloopy/commits/main
