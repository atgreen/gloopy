#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# Gather every third-party license text gloopy vendors/bundles into ./licenses/, preserving
# the source path so each is unambiguous, plus the THIRD-PARTY-LICENSES.md manifest. The
# result is installed into the package (share/doc/gloopy/licenses) and copied into the
# Windows zip, so a distributed build carries the full licensing of the code it ships
# (sfizz + its externals, Surge XT + its sub-libraries, JUCE, the Salamander piano).
#
# Run AFTER the Surge submodule is initialised (scripts/init-surge.sh / build-surge-plugin.sh)
# but it does NOT need JUCE fetched — Surge vendors JUCE under libs/JUCE, the same license.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-licenses}"
rm -rf "$OUT"; mkdir -p "$OUT"

cp THIRD-PARTY-LICENSES.md "$OUT/"

# Every LICENSE / COPYING under the vendored trees, minus test/example fixtures and the
# built Surge LV2 output (whose factory data contains license-named text files).
find third_party -type f \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'LICENCE*' \) \
    -not -path '*/test/*' -not -path '*/tests/*' -not -path '*/example*/*' \
    -not -path '*/surge-plugin/*' -not -path '*/surge-data/*' 2>/dev/null |
while IFS= read -r f; do
    dest="$OUT/$f"
    mkdir -p "$(dirname "$dest")"
    cp "$f" "$dest"
done

# The Salamander Grand Piano's attribution/licence lives in its README (CC-BY 3.0).
if [ -f third_party/SalamanderGrandPiano/README ]; then
    mkdir -p "$OUT/third_party/SalamanderGrandPiano"
    cp third_party/SalamanderGrandPiano/README "$OUT/third_party/SalamanderGrandPiano/"
fi

echo "collect-licenses: $(find "$OUT" -type f | wc -l) files -> $OUT/"
