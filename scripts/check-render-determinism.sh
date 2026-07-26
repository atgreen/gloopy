#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# Composition-as-repo wants **bit-reproducible** offline renders: the same project must
# bounce to byte-identical audio every time (the render path even resets FX state for this).
# Render an example project twice and assert the output matches — so a change that sneaks in
# run-to-run nondeterminism (uninitialised state, a timestamp in the file, unstable ordering,
# an unseeded random source) fails loudly instead of silently corrupting reproducibility.
set -euo pipefail
cd "$(dirname "$0")/.."

GLOOPY="${GLOOPY_BIN:-build/Gloopy_artefacts/Release/gloopy}"
PROJ="${1:-examples/demo-lofi.gloopy}"

[ -x "$GLOOPY" ] || { echo "gloopy binary not found at $GLOOPY (set GLOOPY_BIN)"; exit 2; }
[ -f "$PROJ" ]   || { echo "project not found: $PROJ"; exit 2; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"$GLOOPY" render "$PROJ" "$TMP/a.wav" >/dev/null 2>&1 || { echo "render A failed for $PROJ"; exit 1; }
"$GLOOPY" render "$PROJ" "$TMP/b.wav" >/dev/null 2>&1 || { echo "render B failed for $PROJ"; exit 1; }

A="$(sha256sum "$TMP/a.wav" | cut -d' ' -f1)"
B="$(sha256sum "$TMP/b.wav" | cut -d' ' -f1)"
if [ "$A" = "$B" ]; then
    echo "render determinism OK: $PROJ  ($A)"
else
    echo "NON-DETERMINISTIC render: $PROJ" >&2
    echo "  run A: $A" >&2
    echo "  run B: $B" >&2
    exit 1
fi
