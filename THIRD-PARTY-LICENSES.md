<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Third-party components

Gloopy itself is licensed under the GNU AGPL v3 (see [`LICENSE`](LICENSE)). It
vendors the following third-party source under `third_party/sfizz/`, linked as a
static library. All are permissively licensed and compatible with distribution
inside an AGPL-3.0 work; their original license texts are retained in-tree at the
paths listed below and are authoritative. Gloopy can additionally embed the
**Surge XT** synth engine, which is **GPL-3.0** — see the [Embedded synth
(Surge XT)](#embedded-synth-surge-xt--gpl-30) section below for its combined-work
implications and how to build without it.

| Component | License | License text |
|-----------|---------|--------------|
| **sfizz** (SFZ engine) | BSD-2-Clause | `third_party/sfizz/LICENSE` |
| Abseil (abseil-cpp) | Apache-2.0 | `third_party/sfizz/external/abseil-cpp/LICENSE` |
| SIMDe | MIT | `third_party/sfizz/external/simde/COPYING` |
| st_audiofile | BSD-2-Clause | `third_party/sfizz/external/st_audiofile/LICENSE.md` |
| dr_libs (audio backend) | MIT-0 / public domain | `third_party/sfizz/external/st_audiofile/thirdparty/dr_libs/` |
| ghc filesystem | BSD-3-Clause | `third_party/sfizz/external/filesystem/LICENSE` |
| cephes (math) | permissive | `third_party/sfizz/external/cephes/LICENSE.txt` |
| atomic_queue | MIT | `third_party/sfizz/external/atomic_queue/LICENSE` |
| jsl | permissive | `third_party/sfizz/external/jsl/LICENSE.md` |
| fmidi | Boost Software License 1.0 | `third_party/sfizz/external/fmidi/LICENSE.md` |
| cxxopts | MIT | `third_party/sfizz/external/cxxopts/LICENSE` |

Notes:
- Apache-2.0 and the various MIT/BSD/BSL licenses are one-way compatible into a
  GPL/AGPL-3.0 work, so combining them here is permitted; the obligation is to
  preserve their notices, which the in-tree license files above satisfy.
- sfizz is built **library-only** with the dr_libs audio backend, so the
  optional LGPL dependency **libsndfile** is **not** compiled or linked
  (`SFIZZ_USE_SNDFILE=OFF`), and the plugin wrappers (which would pull in the
  Steinberg VST3 SDK) are disabled.
- JUCE (fetched at build time, not vendored here) is used under its GPL/AGPL
  terms — the reason Gloopy is AGPL-3.0.
- **Local modification:** sfizz vendors its own copy of the Surge *tuning-library*
  (`third_party/sfizz/src/external/tunings/`) in `namespace Tunings`. Because Gloopy
  now also embeds Surge XT — which vendors a *newer* copy of the same library — the
  two collided at link time (an ODR clash: the linker kept sfizz's older strong
  symbol, which Surge then called with a mismatched struct layout, crashing). To fix
  it, sfizz's private copy was renamed to `namespace TuningsSfz` (in `Tunings.h`,
  `Tunings.cpp`, and `sfizz/Tuning.cpp`). sfizz is BSD-2-Clause, which permits
  modification; this note records the change for transparency and future updates.

## Embedded synth (Surge XT) — GPL-3.0

Gloopy incorporates **Surge XT** (GPL-3.0) in **two** distinct forms, both derived
from the `third_party/surge` submodule:

1. **Embedded synth core** — the `surge-common` DSP library is compiled *into* the
   Gloopy binary as its default instrument voice, when `GLOOPY_WITH_SURGE=ON` (the
   default). Only the synth core is compiled here — no JUCE UI, plugin wrappers, or
   standalone.
2. **Bundled Surge XT LV2 plugin** — the *full* Surge XT LV2 plugin (with its JUCE
   editor UI) is built by `scripts/build-surge-plugin.sh`, staged at
   `third_party/surge-plugin/Surge XT.lv2`, and **shipped with the Gloopy binary**
   (installed to `lib/gloopy/plugins`, so it is included in the released RPM/DEB packages;
   on Windows the equivalent is a bundled Surge XT **VST3** under `plugins/`).
   Gloopy hosts it for the real Surge editor via `+ Synth → Surge XT (full editor)`,
   with no external Surge install required. This is a separate GPL-3.0 shared object
   — it is loaded at runtime as an LV2 plugin, not linked into Gloopy's own binary.

| Component | License | License text |
|-----------|---------|--------------|
| **Surge XT** (synth engine + factory patches/wavetables) | **GPL-3.0-or-later** | `third_party/surge/LICENSE` (+ `AUTHORS`) |
| **Surge XT LV2 plugin** (bundled `.so`, incorporates JUCE + Surge's libs) | **GPL-3.0-or-later** | `third_party/surge/LICENSE` (+ `AUTHORS`) |

Surge bundles its own third-party libraries (the `sst-*` DSP libs, fmt, LuaJIT,
PEGTL, r8brain, airwindows, eurorack, zstd, sqlite, tuning-library, …); the license
texts inside the vendored Surge tree are authoritative for those, and Surge's own
`AUTHORS`/`LICENSE` cover the aggregate. The bundled LV2 plugin additionally statically
links **JUCE** (used under its GPL terms — the same GPL/AGPL basis as the rest of the
work); its combined binary is GPL-3.0.

Notes:
- **The shipped Gloopy binary is a combined GPL-3.0 / AGPL-3.0 work** (embedded
  `surge-common`), and the **released packages additionally ship the GPL-3.0 Surge XT
  LV2 plugin** beside it. GPLv3 §13 and the GNU AGPLv3 are explicitly compatible for
  combination, so linking Surge (GPL-3.0) into Gloopy (AGPL-3.0) and shipping the Surge
  plugin alongside it are both permitted; the conveyed result must satisfy both licenses
  (in practice, the AGPL for Gloopy's own code, the GPL for Surge's, **source available
  for the whole** — including the bundled plugin, whose source is the `third_party/surge`
  submodule at the pinned commit). Building with `-DGLOOPY_WITH_SURGE=OFF` and without
  running `build-surge-plugin.sh` produces a lean binary with **no Surge code**, if a
  GPL-free build is desired.
- **Factory content:** only Surge's **first-party** factory patches
  (`resources/data/patches_factory`) and wavetables are used/bundled — these ship
  under the repo's GPL-3.0. Third-party patch/wavetable packs
  (`*_3rdparty`) are **not** included, since they carry their own terms.
- See `docs/surge-embed.md` for the build/vendoring details.

## Bundled instrument (sample data)

Gloopy also vendors one sampled instrument, used by the built-in **Piano + Bass +
Drums** template:

| Component | License | Attribution | In-tree |
|-----------|---------|-------------|---------|
| **Salamander Grand Piano V3** (Yamaha C5, OggVorbis) | CC-BY 3.0 | Alexander Holm; retuned by Markus Fiedler | `third_party/SalamanderGrandPiano/` (see its `README`) |

- The Salamander Grand Piano is licensed under the Creative Commons Attribution
  3.0 license (<https://creativecommons.org/licenses/by/3.0/>). CC-BY permits
  redistribution — including inside this AGPL-3.0 work — provided attribution is
  preserved, which the table above and the vendored `README` satisfy. The samples
  are data, not linked code, so they place no license obligation on Gloopy's own
  source.

Gloopy also vendors two **Hydrogen** drumkits, used by the built-in drum kit (the
default sounds) and loadable via *Load Hydrogen kit…*:

| Component | License | Attribution | In-tree |
|-----------|---------|-------------|---------|
| **GMRockKit** (sampled 5-piece acoustic kit) | GPL | Glen MacArthur / Sebastian Moors | `third_party/drumkits/GMRockKit/` (see its `drumkit.xml`) |
| **TR808EmulationKit** (analog 808 emulation) | GPL | ArtemioLabs (<http://artemiolabs.com>) | `third_party/drumkits/TR808EmulationKit/` (see its `drumkit.xml`) |

- Both kits ship from the [Hydrogen](https://github.com/hydrogen-music/hydrogen)
  project under the **GNU General Public License**. GPL is one-way compatible into this
  AGPL-3.0 work, so bundling them is permitted; the copyright/GPL notices in each kit's
  `drumkit.xml` and the attribution above are preserved. The samples are data, not
  linked code.

## Control-API libraries (gRPC / Protobuf / Abseil)

| Component | License | Notes |
|-----------|---------|-------|
| **gRPC** | Apache-2.0 | the control API's structural lane |
| **Protocol Buffers** | BSD-3-Clause | wire format for gRPC |
| **Abseil** | Apache-2.0 | gRPC/Protobuf dependency |

On **Linux** these come from the distribution's own packages (`grpc`, `protobuf`), so
the RPM/DEB **depend** on them rather than bundling them — the system carries their
licenses. On **Windows** they are **statically linked** from vcpkg, so their copyright
texts are bundled into the zip under `licenses/vcpkg/`. Apache-2.0 and BSD-3-Clause are
one-way compatible into a GPL/AGPL-3.0 work.

## Where the license texts ship

`packaging/collect-licenses.sh` gathers every in-tree third-party license text into
`licenses/`, which is installed to **`share/doc/gloopy/third-party-licenses/`** in the
RPM/DEB (alongside this manifest) and copied into the Windows zip's `licenses/` folder —
so a distributed build always carries the full licensing of the code it ships.
