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

_Nothing yet._

## [0.1.0] - 2026-07-30

First release.

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
- **About dialog.** File → About Gloopy shows the version, copyright, license, bundled
  third-party components, and the written offer of source (AGPL §13).
- Third-party license texts are bundled into every package and the Windows build.
- Supply chain: RPM/DEB packages and the repo metadata are GPG-signed, and release
  artifacts ship with an SPDX **SBOM** and **Sigstore** build-provenance attestations
  (verify with `gh attestation verify <file> --repo atgreen/gloopy`).

### Changed
- Packaging moved off CPack to a traditional `rpmbuild`/`dpkg-buildpackage` flow that
  ships only Gloopy's own files (no vendored headers or build tooling).

### Fixed
- Numerous portability fixes for the first MSVC/Windows build (CRT-runtime and zlib
  link conflicts, header/name collisions, and non-standard-C++ constructs), all scoped
  so the Linux build is unaffected.

[Unreleased]: https://github.com/atgreen/gloopy/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/atgreen/gloopy/releases/tag/v0.1.0
