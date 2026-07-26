#!/usr/bin/env bash
# Initialize ONLY the Surge sub-submodules that the embedded synth core
# (surge-common) needs to build. Run this after cloning Gloopy if you want the
# bundled Surge XT synth (GLOOPY_WITH_SURGE=ON, the default).
#
# Do NOT `git clone --recursive` / `git submodule update --init --recursive` the
# whole tree: that would pull Surge's own libs/JUCE (~500 MB), which surge-common
# does NOT use (it builds without JUCE). This script fetches just the needed libs.
#
# Optional speed-up: set GLOOPY_SURGE_REFERENCE to a local Surge clone (or have
# ~/git/surge) and its objects are borrowed instead of re-downloaded.
set -euo pipefail
cd "$(dirname "$0")/.."

# 1) The Surge source submodule itself (pinned commit).
git submodule update --init third_party/surge

# 2) Only the sub-submodules surge-common links (see docs/surge-embed.md).
NEED=(libs/simde libs/fmt libs/luajitlib libs/pffft libs/airwindows libs/binn
      libs/eurorack libs/sst libs/oddsound-mts libs/tuning-library libs/PEGTL
      libs/zstd libs/r8brain-free-src)

REF=()
_ref="${GLOOPY_SURGE_REFERENCE:-$HOME/git/surge}"
[ -d "$_ref/.git" ] && REF=(--reference "$_ref")

git -C third_party/surge submodule update --init --recursive "${REF[@]}" -- "${NEED[@]}"
echo "init-surge: done (Surge libs ready; JUCE intentionally skipped)."
