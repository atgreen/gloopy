# Install Gloopy

On Linux, the easiest route is to add the Gloopy **package repository** — you get
`gloopy` on your `PATH` and future releases arrive with your normal system updates.
The repositories are hosted on GitHub Pages; the packages are the full build, with
the bundled Surge XT synth.

| System | Download | Includes |
|--------|----------|----------|
| **Fedora / RHEL** | `gloopy-<version>.x86_64.rpm` | Full build, with the bundled Surge XT synth |
| **Debian / Ubuntu** | `gloopy_<version>_amd64.deb` | Full build, with the bundled Surge XT synth |
| **Windows (x64)** | `gloopy-windows-x64.zip` | Portable build, with the bundled Surge XT synth (VST3) |

!!! note "Signed repositories"
    The `dnf`/`apt` repositories work when the release was built with a GPG signing
    key (the committed repo config verifies signatures). If a release is published
    **unsigned**, the repo steps below fail their signature check and the apt keyring
    download 404s — in that case use the one-off `.rpm`/`.deb` download from the
    [Releases page](https://github.com/atgreen/gloopy/releases) instead, which needs no
    signature.

## Fedora / RHEL (RPM)

Add the Gloopy repository and install — future releases then update with
`dnf upgrade`:

```sh
sudo dnf config-manager addrepo --from-repofile=https://atgreen.github.io/gloopy/rpm-repo/gloopy.repo
sudo dnf install gloopy
```

When the packages are GPG-signed, dnf imports the signing key from the repository
automatically on first install.

It installs to `/usr/bin/gloopy`. Launch it from a terminal:

```sh
gloopy
```

Prefer a one-off download? Grab the `.rpm` from the
[Releases page](https://github.com/atgreen/gloopy/releases) and
`sudo dnf install ./gloopy-*.rpm` instead.

## Debian / Ubuntu (DEB)

Add the Gloopy repository and install — future releases then update with
`apt upgrade`:

```sh
curl -fsSL https://atgreen.github.io/gloopy/deb-repo/gloopy-archive-keyring.gpg | sudo tee /usr/share/keyrings/gloopy-archive-keyring.asc > /dev/null
echo "deb [signed-by=/usr/share/keyrings/gloopy-archive-keyring.asc] https://atgreen.github.io/gloopy/deb-repo stable main" | sudo tee /etc/apt/sources.list.d/gloopy.list
sudo apt update
sudo apt install gloopy
```

Prefer a one-off download? Grab the `.deb` from the
[Releases page](https://github.com/atgreen/gloopy/releases) and
`sudo apt install ./gloopy_*.deb` instead.

## Windows

Two downloads on the [Releases page](https://github.com/atgreen/gloopy/releases),
both x64:

| Download | Use it when |
|----------|-------------|
| **`gloopy-setup-x64.exe`** (installer) | The usual choice — installs Gloopy, adds a Start-menu shortcut and an uninstaller, and can put `gloopy` on your `PATH`. |
| **`gloopy-windows-x64.zip`** (portable) | You want no install — unzip and run, e.g. from a USB stick. |

### Installer

1. Download and run **`gloopy-setup-x64.exe`**.
2. On the setup screen, tick **"Add Gloopy to my PATH"** if you want the `gloopy`
   command line and the [MCP server](../control-scripting/how-to/mcp-server.md) to
   work by name (recommended).
3. Finish, and launch Gloopy from the Start menu.

It's a per-user install (no administrator prompt) and uninstalls cleanly from
**Settings → Apps**. Because it isn't code-signed yet, Windows SmartScreen may warn
about an unknown publisher — choose **More info → Run anyway**.

### Portable zip

1. Download `gloopy-windows-x64.zip` and unzip it anywhere.
2. Run **`gloopy.exe`** from the unzipped folder. (For the `gloopy` command line /
   MCP server, add that folder to your `PATH` yourself, or use the installer.)

Both include everything Gloopy needs:

- The **[Microsoft Visual C++ 2015–2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)**
  is the one external requirement, and most machines already have it.
- The featured **Surge XT** synth is bundled as a VST3 (in `plugins\`), so
  **+ Synth → Surge XT** works out of the box — alongside the built-in Basic
  synth, the sampler, SFZ instruments, and hosted VST3 plugins.

## macOS

No prebuilt package yet — [build from source](#build-from-source).

## Running Gloopy

Once installed, `gloopy` opens the app. It also takes a project and has headless
modes:

```sh
gloopy                                  # open the app
gloopy my-song/                         # open a project (a folder or a .gloopy)
gloopy render my-song/ out.wav          # bounce the mix, no window
gloopy render my-song/ out.wav --range 0 16   # just beats 0–16
gloopy export-stems my-song/ stems/     # one WAV per track
```

On startup Gloopy also opens its [control API](../control-scripting/index.md) —
**OSC** on UDP `9000` and **gRPC** on `127.0.0.1:50051` — so scripts can drive it.

## Build from source

Prefer to build it yourself (or on macOS)? The repository's
[**README**](https://github.com/atgreen/gloopy#building) has the current, complete
instructions — the dependencies, the CMake commands, and how the bundled Surge XT
synth is built. In short: install the build dependencies, then

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Gloopy_artefacts/Release/gloopy
```

## Next steps

- New here? Do [Your first track](tutorials/first-track.md).
- Unsure which feature does what? [What to use for what](what-to-use-for-what.md).
