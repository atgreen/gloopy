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

## [0.1.7] - 2026-08-01

### Fixed
- **The hosted Surge XT synth now shows its factory patches.** When Surge XT is loaded
  by a host, its data lookup (relative to its own binary) resolves to *Gloopy* rather than
  the plugin, so it never finds the `SurgeXTData` bundled beside it and fell through to
  empty system paths — an empty patch browser. Gloopy now sets the `SURGE_DATA_HOME`
  environment variable (which Surge honours above all other paths, on every platform) to
  the bundled data at startup, so the featured Surge instrument comes up fully populated.

### Changed
- **`gloopy-connect` (Emacs) opens the REPL in the kernel package with a Gloopy banner.**
  Attaching Sly now drops you into `gloopy-kernel` — where a clip's generators live — instead
  of `CL-USER`, and replaces SLY's startup banner with a short Gloopy one (customisable via
  `gloopy-repl-package` and `gloopy-replace-banner`). Scoped to the connection `gloopy-connect`
  opened, so other Sly REPLs are untouched.

## [0.1.6] - 2026-08-01

### Fixed
- **Gloopy no longer fails silently when port 50051 is in use.** Its gRPC control server
  binds `127.0.0.1:50051`, and the script-clip kernel is launched only once that bind
  succeeds — so if another service had taken 50051 (e.g. a container publishing that port),
  Gloopy came up with **no kernel and no clear reason**, and generation / the Emacs–Slynk
  connection just didn't work. Gloopy now **falls back to an OS-assigned free port** when
  50051 is busy, publishes the actual port in its `kernel.json` discovery file, and passes
  it to the kernels. Emacs (`gloopy.el`) is unaffected — it already reads the kernel's Slynk
  port from that file. The **Python and Common Lisp clients** now auto-discover the control
  port from the discovery file (`Gloopy()` / `(connect)`), falling back to 50051, so external
  scripts follow a moved port too.

## [0.1.5] - 2026-08-01

Faster, self-contained script-clip kernels: their caches now live under
`~/.cache/gloopy/`.

### Changed
- **Script-clip kernels keep their caches under `~/.cache/gloopy/`.** The SBCL kernel
  writes its ASDF fasl cache to `~/.cache/gloopy/common-lisp` and resolves its Lisp
  dependencies from ocicl's global collection at `~/.cache/gloopy/ocicl` (forced global,
  `-g`, mode); the Python kernel runs in a shared venv at `~/.cache/gloopy/python`,
  auto-created and provisioned with grpcio/protobuf/ipykernel (falling back to the system
  interpreter if it can't be built). Dedicated, persistent caches mean the dependencies are
  fetched and compiled once and reused, so repeat startups are fast and independent of the
  user's global ocicl setup or Python site-packages.

## [0.1.4] - 2026-08-01

A follow-up to 0.1.3: the script-clip kernel now starts when Gloopy is launched
from the desktop, and the project describes itself as "composition-as-code".

### Fixed
- **Script-clip kernels now start when Gloopy is launched from the desktop.** The SBCL and
  Python kernels were launched inheriting Gloopy's working directory, but the
  application-menu launcher starts Gloopy in `/` — where ocicl (which the kernel loads from
  `~/.sbclrc`, and which resolves the kernel's Lisp dependencies relative to the current
  directory) can't operate. The kernel died on startup, so generation and *Connect Emacs to
  Kernel* failed with "Slynk did not start (is SBCL installed, with Slynk available?)" even
  when SBCL and Slynk were installed. Kernels now launch in a writable per-user directory
  (`~/.cache/gloopy/kernel`), so they start regardless of how Gloopy was opened.

### Changed
- Renamed the project's tagline and terminology from **composition-as-repo** to
  **composition-as-code** across the app, packages, documentation, and control-API comments.

## [0.1.3] - 2026-08-01

Notebook-driven live coding and a musical vocabulary for the clients: a headless
Python kernel (with attach-to-live notebooks) beside the Lisp one, note-name /
scale / chord / mini-notation helpers, named generators and self-contained
"song-as-repo" projects, and the script-clip kernels now bundled in the packages.

### Added
- **Musical note helpers in the Python and Common Lisp clients.** A pure,
  dependency-free layer for describing music by name instead of raw numbers:
  note names (`"C#4"`), duration shorthand (`q`, `8t`, dotted `q.`), `scale` and
  `chord` builders, a `seq` step-sequencer, and a compact `mini` note-string DSL
  (`mini("c4q d e f g4h a b")` — sticky letter-durations, chords, and rests).
  `note()` now also accepts a pitch name and a duration code. Documented once,
  language-agnostically, in the new *Describing notes* reference page. The wire
  format is unchanged (MIDI pitch + beats).
- The script-clip **kernels are now shipped in the packages** (`share/gloopy/`), and the
  Emacs helper `gloopy.el` installs into Emacs's `site-lisp` — so script clips and
  `M-x gloopy-connect` work from an installed Gloopy, not just a source checkout.
- **Python script clips generate headlessly, out of the box.** Gloopy now auto-launches a
  resident Python kernel — the twin of the warm SBCL kernel — so a Python-language clip
  generates with zero setup instead of hanging until a notebook attaches. The same kernel
  can run as a Jupyter kernel (*attach-to-live*): a notebook attaches to that exact process
  and edits its generators live, the Python analogue of Emacs attaching to the SBCL image
  over Slynk. One Python kernel serves at a time — a notebook that runs `gloopy.attach()`
  preempts the headless one, so no two race for a job. *File → Open Python Notebook…* ensures
  the kernel and shows the `jupyter console --existing` command.
- **Live script clips from a Jupyter notebook (Python).** `gloopy.attach()` connects a
  notebook/REPL as Gloopy's live Python generator kernel — register a generator with
  `@k.generator`, and redefining it in a cell is picked up on the next generate (and by
  **Live** clips a bar ahead of playback), the Python analogue of the Emacs/Sly live-image
  loop. Generate jobs are now **routed by language**, so a Python kernel and the SBCL kernel
  coexist. See `python/notebooks/live-clips.ipynb`.
- **Clips can be driven by a named generator.** Instead of each script clip carrying its own
  source file, a clip can name a generator in the project's package/module
  (`myproject.gens:arp` in Python, `myproject:arp` in Lisp, with an optional system/import
  root to load first), so one reusable generator can drive several clips. Assign one from the
  desktop via the arrangement clip's **Set script generator…** menu item; a clip is either a
  source file or a named generator (setting one clears the other, preserving the seed).
- **Compositions are self-contained, reproducible "song-as-repo" folders.** A clip's
  generator source now lives inside the project (`<project>/scripts/`, stored as a
  project-relative path) rather than a global directory, and saving copies each clip's script
  into the folder — so a cloned composition carries every source needed to regenerate its
  clips. The composition `.gitignore` now excludes the heavy, platform-specific installed
  environments (`.venv/`, ocicl systems, `__pycache__`, `.ipynb_checkpoints`) while keeping
  authored `scripts/` and lockfiles committed, and a starter `README.md` is written on first
  save.

### Fixed
- The status bar's "λ Slynk" kernel indicator now clears when the resident kernel is
  killed or crashes (previously it stayed stale), and the warm kernel is automatically
  respawned — so the indicator drops and then returns with the new port. The stale
  discovery file is removed too, so Emacs won't try to attach to a dead port.
- Clicking to create a clip now lands it in the bar you clicked. Click-to-create rounded to
  the nearest bar, so a click in the second half of a bar jumped the new clip into the next
  one; it now floors to the bar under the cursor (drag/resize/loop snapping still rounds to
  nearest).

### Changed
- Internal: removed the vestigial `Kernel` gRPC service from the proto (the abandoned
  kernel-as-server design; the kernel is a client of the `Gloopy` service) and the dead
  Lisp handlers that went with it.

## [0.1.2] - 2026-07-31

Scripting comes to Gloopy: clips whose notes are generated by code, with a live
Common Lisp kernel you can drive interactively from Emacs — plus Linux desktop
integration and keyboard shortcuts.

### Added
- **Script clips (experimental).** A clip's notes can be generated by code. Right-click a
  clip → *Edit script code...* opens a Common Lisp source in Emacs, and *Generate from
  script* runs it through a bundled SBCL kernel and materialises the returned notes.
  Also drivable over the control API (`RegenerateClip`). Deterministic via a per-clip seed;
  the generated notes are cached in the project so the clip plays without the runtime. A
  resident "warm" kernel (auto-started when SBCL is installed) keeps generation fast — the
  first generate compiles once, the rest are instant.
  Generators can be written in Common Lisp (bundled SBCL kernel) or Python. *Live-drive
  from script* plays a clip's script live during playback (ephemeral, not materialised)
  through the real-time note lane.
- **Emacs/Sly integration for script clips.** The resident Lisp kernel also hosts a
  **Slynk** server, and a bundled Emacs package (`emacs/gloopy.el`) connects to it:
  `M-x gloopy-connect` (or *File → Connect Emacs to Kernel (Slynk)*) attaches Sly/SLIME to
  the very image that generates your clips. The kernel is a genuine **live image** — it
  loads a clip's source once, then a generator you redefine in Emacs (`C-c C-c`) is what the
  next generate runs, no restart. While connected, clicking a script clip in Gloopy opens
  its source in Emacs. A "λ Slynk <port>" indicator in the status bar shows the kernel is
  ready.
- **"Live" script clips.** Right-click a script clip → *Live (auto-generate on playback)*
  (a **LIVE** badge appears). While the transport rolls, Gloopy re-runs the clip's generator
  from the live image about a bar before it plays and swaps in the fresh notes — so a looping
  clip reflects each `C-c C-c` on the next pass with no *Generate* click, falling back to the
  cached notes if a generate runs long. Also over the API as `SetClipScriptLive`.
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

[Unreleased]: https://github.com/atgreen/gloopy/compare/v0.1.7...HEAD
[0.1.7]: https://github.com/atgreen/gloopy/compare/v0.1.6...v0.1.7
[0.1.6]: https://github.com/atgreen/gloopy/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/atgreen/gloopy/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/atgreen/gloopy/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/atgreen/gloopy/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/atgreen/gloopy/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/atgreen/gloopy/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/atgreen/gloopy/releases/tag/v0.1.0
