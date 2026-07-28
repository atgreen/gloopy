#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# Build Surge XT as a *VST3* and stage it (+ its first-party factory data) at
# third_party/surge-plugin/ so the Windows release job can drop it into the zip's
# plugins/ folder — the Windows counterpart to build-surge-plugin.sh (which builds
# the Linux .lv2). Windows hosts VST3, not LV2, so "+ Synth -> Surge XT (full
# editor)" needs the VST3 bundled.
#
# Runs on the GitHub windows-latest runner under Git Bash (find/cp/tar are present);
# the actual compile uses the Visual Studio generator. Like the LV2 build, the full
# plugin needs JUCE (its UI), which init-surge.sh skips — we add it below.
set -euo pipefail
cd "$(dirname "$0")/.."
SURGE=third_party/surge
BUILD="${1:-build-surge-vst3}"

# 1) Source: surge submodule + the sub-libs surge-common needs + JUCE (for the UI).
#    Mirrors build-surge-plugin.sh — proven to give the plugin everything it needs.
bash scripts/init-surge.sh
git -C "$SURGE" submodule update --init --recursive -- libs/JUCE

# 2) Configure: VST3 synth only (+ FX, because src/cmake/pluginval.cmake references the
#    surge-fx target unconditionally). No LV2/CLAP/standalone/tests. The Visual Studio
#    generator is multi-config, so the build below passes --config Release.
cmake -S "$SURGE" -B "$BUILD" -G "Visual Studio 17 2022" -A x64 \
  -DSURGE_BUILD_XT=ON -DSURGE_SKIP_VST3=FALSE -DSURGE_BUILD_LV2=OFF \
  -DSURGE_BUILD_CLAP=OFF -DSURGE_BUILD_FX=ON -DSURGE_SKIP_STANDALONE=TRUE \
  -DSURGE_BUILD_TESTRUNNER=OFF -DSURGE_INCLUDE_MELATONIN_INSPECTOR=OFF \
  -DSURGE_SKIP_WERROR=TRUE -DBUILD_TESTING=OFF -DENABLE_LTO=OFF

# 3) Build (produces the VST3 bundle under the build tree).
cmake --build "$BUILD" --config Release

# 4) Stage the Surge XT synth .vst3 bundle (exclude the FX plugin) into third_party/surge-plugin/.
mkdir -p third_party/surge-plugin
found=0
while IFS= read -r vst3; do
    case "$(basename "$vst3")" in
        *FX*|*Effects*) continue ;;      # ship only the synth, not the FX plugin
    esac
    cp -r "$vst3" third_party/surge-plugin/ && { echo "staged: $vst3"; found=1; break; }
done < <(find "$BUILD" -name '*.vst3' -type d)
if [ "$found" != 1 ]; then
    echo "no synth .vst3 produced under $BUILD; VST3 bundles found:" >&2
    find "$BUILD" -name '*.vst3' >&2 || true
    exit 1
fi

# 5) Stage first-party factory data as SurgeXTData beside the .vst3, for Surge's portable mode.
#    At construction SurgeStorage walks UP from its DLL directory looking for a SurgeXTData/
#    dir (src/common/SurgeStorage.cpp), so the plugins/ sibling — plugins/SurgeXTData, once the
#    release job copies surge-plugin/* into plugins/ — is found with NO external install. First-
#    party content ONLY: the *_3rdparty packs carry their own licenses and are excluded (keeps
#    the bundle GPL-3-clean; see THIRD-PARTY-LICENSES.md). configuration.xml is compiled in.
DATASRC="$SURGE/resources/data"
if [ -d "$DATASRC" ]; then
    DATADST="third_party/surge-plugin/SurgeXTData"
    rm -rf "$DATADST"; mkdir -p "$DATADST"
    # tar keeps the exclude logic identical to the Linux script.
    ( cd "$DATASRC" && tar -cf - \
        --exclude=patches_3rdparty --exclude=wavetables_3rdparty --exclude=impulses_3rdparty . ) \
      | ( cd "$DATADST" && tar -xf - )
    echo "staged data: $DATADST (first-party only)"
else
    echo "WARNING: $DATASRC not found — Surge patch/wavetable browser will be empty" >&2
fi
echo "build-surge-vst3: done -> third_party/surge-plugin/"
