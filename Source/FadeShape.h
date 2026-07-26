// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <cmath>

/** Warp a linear fade position `t` (0 = silent edge .. 1 = full level) into a gain
    following a fade *shape*. Pure + branch-simple so it unit-tests without the engine
    and both fade edges (in and out, each fed their own rising `t`) share one curve.

      0 linear       — g = t                       (constant-slope amplitude ramp)
      1 equal-power  — g = sin(t·π/2)              (constant-power; the crossfade default,
                       always ≥ linear, so an equal-power fade-in is louder than linear)
      2 exponential  — g = t²                       (slow, gentle start — smooth for fade-outs)

    Prior art: equal-power (sin/cos) is the standard constant-power crossfade law used by
    Ardour/Reaper/Pro Tools; the t² "exponential" curve matches their gentle/slow option. */
inline float fadeShapeGain (int shape, float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    switch (shape)
    {
        case 1:  return std::sin (t * juce::MathConstants<float>::halfPi);   // equal-power
        case 2:  return t * t;                                               // exponential
        default: return t;                                                   // linear
    }
}
