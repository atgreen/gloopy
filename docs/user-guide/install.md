# Install Gloopy

Prebuilt packages are attached to each tagged release on the
[**Releases page**](https://github.com/atgreen/gloopy/releases). Grab the one for
your system.

| System | Download | Includes |
|--------|----------|----------|
| **Fedora / RHEL** | `gloopy-<version>.x86_64.rpm` | Full build, with the bundled Surge XT synth |
| **Debian / Ubuntu** | `gloopy_<version>_amd64.deb` | Full build, with the bundled Surge XT synth |
| **Windows (x64)** | `gloopy-windows-x64.zip` | Portable build (see the note below) |

## Fedora / RHEL (RPM)

Download the `.rpm` from the latest release, then install it — dependencies
resolve automatically from your repositories:

```sh
sudo dnf install ./gloopy-*.rpm
```

It installs to `/usr/bin/gloopy`. Launch it from a terminal:

```sh
gloopy
```

## Debian / Ubuntu (DEB)

Download the `.deb`, then:

```sh
sudo apt install ./gloopy_*.deb
```

`apt` pulls in the required libraries. Then run `gloopy`.

## Windows (portable zip)

1. Download `gloopy-windows-x64.zip` and unzip it anywhere.
2. Run **`gloopy.exe`** from the unzipped folder.

The zip is self-contained — the app, its runtime libraries, and its assets. Two
things to know:

- It needs the **[Microsoft Visual C++ 2015–2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)**,
  which most machines already have.
- The **Surge XT** synth isn't bundled on Windows yet. Everything else — the
  built-in Basic synth, the sampler, SFZ instruments, and hosted VST3 plugins —
  works.

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
