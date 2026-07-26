// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cmath>

// Pure phaser DSP primitives, factored out of PhaserFx so they're unit-testable without
// the audio engine. A phaser cascades first-order allpass stages whose notch frequency is
// swept by an LFO; summing the allpass output with the dry signal makes moving notches.

// One first-order allpass stage in one-multiply form. It is magnitude-preserving for all
// frequencies (the allpass property) and only shifts phase. `z` is the stage's state.
//   y[n] = a*x[n] + z ;  z = x[n] - a*y[n]
inline float allpassStage (float x, float a, float& z)
{
    const float y = a * x + z;
    z = x - a * y;
    return y;
}

// Allpass coefficient for a break frequency fc (Hz) at sample rate sr, in (-1, 1):
// fc -> 0 gives a -> -1, fc -> sr/2 gives a -> 1.
inline float phaserCoeff (float fc, double sr)
{
    const float t = std::tan (3.14159265358979323846f * fc / (float) sr);
    return (t - 1.0f) / (t + 1.0f);
}
