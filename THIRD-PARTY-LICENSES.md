<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Third-party components

Gloopy itself is licensed under the GNU AGPL v3 (see [`LICENSE`](LICENSE)). It
vendors the following third-party source under `third_party/sfizz/`, linked as a
static library. All are permissively licensed and compatible with distribution
inside an AGPL-3.0 work; their original license texts are retained in-tree at the
paths listed below and are authoritative.

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
