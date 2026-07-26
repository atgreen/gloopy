#!/usr/bin/env bash
# Build the Surge XT *LV2 plugin* from the third_party/surge submodule and stage it
# for bundling with Gloopy (so Gloopy can host it -> the real Surge XT editor UI).
#
# Unlike the headless synth core (surge-common), the full plugin builds its UI and
# therefore needs the GUI build stack that surge-common does NOT: OpenGL + the X11
# dev libraries (plus JUCE, which init-surge.sh skips). Install first:
#   Fedora:  dnf install -y mesa-libGL-devel libXrender-devel libXrandr-devel \
#                          libXinerama-devel libXcursor-devel libXext-devel \
#                          freetype-devel alsa-lib-devel
#   Debian:  apt-get install -y libgl1-mesa-dev libxrender-dev libxrandr-dev \
#                          libxinerama-dev libxcursor-dev libxext-dev \
#                          libfreetype-dev libasound2-dev
#
# Optional: set GLOOPY_SURGE_REFERENCE (or have ~/git/surge) to borrow git objects.
set -euo pipefail
cd "$(dirname "$0")/.."
SURGE=third_party/surge
BUILD="${1:-build-surge-plugin}"

# 1) Source: surge submodule + the sub-libs surge-common needs + JUCE (for the UI).
bash scripts/init-surge.sh
_ref="${GLOOPY_SURGE_REFERENCE:-$HOME/git/surge}"
REF=(); [ -d "$_ref/.git" ] && REF=(--reference "$_ref")
git -C "$SURGE" submodule update --init --recursive "${REF[@]}" -- libs/JUCE

# 2) Configure: LV2 synth (+ FX) only. No VST3/CLAP/standalone/tests. FX is ON only
#    because src/cmake/pluginval.cmake references the surge-fx target unconditionally.
cmake -S "$SURGE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSURGE_BUILD_XT=ON -DSURGE_BUILD_LV2=ON -DSURGE_BUILD_FX=ON \
  -DSURGE_SKIP_VST3=TRUE -DSURGE_BUILD_CLAP=OFF \
  -DSURGE_BUILD_TESTRUNNER=OFF -DSURGE_SKIP_STANDALONE=TRUE \
  -DSURGE_INCLUDE_MELATONIN_INSPECTOR=OFF -DSURGE_SKIP_WERROR=TRUE \
  -DBUILD_TESTING=OFF -DENABLE_LTO=OFF

# 3) Build (produces the LV2 bundle under the build tree).
cmake --build "$BUILD"

# 4) Stage the Surge XT .lv2 bundle for Gloopy to ship + host.
mkdir -p third_party/surge-plugin
found=0
while IFS= read -r lv2; do
    case "$(basename "$lv2")" in
        *FX*|*Effects*) continue ;;      # ship only the synth, not the FX plugin
    esac
    cp -r "$lv2" third_party/surge-plugin/ && { echo "staged: $lv2"; found=1; }
done < <(find "$BUILD" -name '*.lv2' -type d)
[ "$found" = 1 ] || { echo "no .lv2 produced under $BUILD" >&2; exit 1; }

# 5) Stage Surge's factory data as SurgeXTData/ beside the .lv2. Surge XT finds it via
#    "portable mode": at construction it walks UP from its own .so directory looking for a
#    SurgeXTData/ dir (SurgeStorage.cpp), so bundling it here -> installed to bin/plugins/
#    SurgeXTData -> the factory patch/wavetable browser is populated with NO external install.
#    First-party content ONLY: the *_3rdparty packs carry their own licenses and are excluded
#    (keeps the bundle GPL-3-clean; see THIRD-PARTY-LICENSES.md). configuration.xml is NOT here
#    — Surge compiles it in as a binary resource.
DATASRC="$SURGE/resources/data"
DATADST=third_party/surge-plugin/SurgeXTData
if [ -d "$DATASRC" ]; then
    rm -rf "$DATADST"; mkdir -p "$DATADST"
    # tar is portable across the CI containers (rsync is not guaranteed installed).
    ( cd "$DATASRC" && tar -cf - \
        --exclude=patches_3rdparty --exclude=wavetables_3rdparty --exclude=impulses_3rdparty . ) \
      | ( cd "$DATADST" && tar -xf - )
    echo "staged data: $DATADST ($(du -sh "$DATADST" | cut -f1), first-party only)"
else
    echo "WARNING: $DATASRC not found — Surge patch/wavetable browser will be empty" >&2
fi
echo "build-surge-plugin: done -> third_party/surge-plugin/"
