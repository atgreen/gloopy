// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// Resampling interpolation kernels for the Sampler (played at a fractional rate). Pure so the
// math is unit-testable without the audio buffer. `t` in [0,1) between p1 and p2; p0/p3 are the
// neighbours. Catmull-Rom is a 4-point cubic that passes through p1 (t=0) and p2 (t=1) and is
// C1-continuous across segments (no slope kink at a sample boundary, unlike linear), so a
// pitch-shifted sample is smoother / less distorted.
namespace gloopy
{
    inline float catmullRom (float p0, float p1, float p2, float p3, float t) noexcept
    {
        return 0.5f * ((2.0f * p1)
                       + (-p0 + p2) * t
                       + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                       + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
    }
}
