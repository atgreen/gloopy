// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// Pure mid/side stereo-widen of one L/R sample pair. Width 0 = mono (side removed),
// 1 = unchanged, 2 = double-wide. The mono sum (L+R) = 2*mid is invariant of Width,
// so widening never shifts the centre or the mono downmix. Kept as a free function
// in its own header so it is unit-testable without the audio-processor dependency
// that Effects.h/Effect.h pull in.
inline void widenSample (float& l, float& r, float width)
{
    const float mid  = 0.5f * (l + r);
    const float side = 0.5f * (l - r) * width;
    l = mid + side;
    r = mid - side;
}
