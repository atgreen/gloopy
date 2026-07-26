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
echo "build-surge-plugin: done -> third_party/surge-plugin/"
