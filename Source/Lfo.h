// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cmath>

// Pure LFO math for the modulation matrix, factored out so it's unit-testable without
// the audio engine. A *tempo-synced* LFO (syncBeats > 0) takes its phase from the beat
// position — one cycle per syncBeats beats — so its period tracks the tempo. A *free*
// LFO uses rate (Hz) against transport seconds. See MainComponent::evaluateModulation.
inline double lfoPhaseCycles (float syncBeats, double beatPos, float rate, double timeSeconds)
{
    return syncBeats > 0.0f ? beatPos / (double) syncBeats
                            : (double) rate * timeSeconds;
}

inline double lfoOsc (int shape, double phaseCycles)   // -> bipolar [-1, 1]
{
    const double frac = phaseCycles - std::floor (phaseCycles);   // 0..1
    switch (shape)
    {
        case 1:  return 4.0 * std::abs (frac - 0.5) - 1.0;                       // triangle
        case 2:  return 2.0 * frac - 1.0;                                        // saw
        case 3:  return frac < 0.5 ? 1.0 : -1.0;                                 // square
        default: return std::sin (2.0 * 3.14159265358979323846 * frac);         // sine
    }
}
