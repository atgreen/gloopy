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

// Deterministic pseudo-random value in [-1, 1] for a given integer step. A fixed
// integer bit-mix (splitmix-style) — no global RNG state — so a sample-and-hold LFO
// renders byte-identically every time (principle: offline bounces are reproducible).
inline double lfoHash (long long step)
{
    unsigned long long h = (unsigned long long) step * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27; h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return ((double) (h >> 11) / (double) (1ULL << 53)) * 2.0 - 1.0;   // [-1, 1)
}

inline double lfoOsc (int shape, double phaseCycles)   // -> bipolar [-1, 1]
{
    const double frac = phaseCycles - std::floor (phaseCycles);   // 0..1
    switch (shape)
    {
        case 1:  return 4.0 * std::abs (frac - 0.5) - 1.0;                       // triangle
        case 2:  return 2.0 * frac - 1.0;                                        // saw
        case 3:  return frac < 0.5 ? 1.0 : -1.0;                                 // square
        case 4:  return lfoHash ((long long) std::floor (phaseCycles));          // sample-and-hold (stepped random)
        default: return std::sin (2.0 * 3.14159265358979323846 * frac);         // sine
    }
}

// The modulation depth multiplier: the oscillator shifted by a phase offset (0..1 of a
// cycle) and, when unipolar, folded to [0, 1] so the value stays on one side of centre
// (centre .. centre+depth) instead of swinging ± depth. The caller forms
// value = centre + depth * lfoUnit(...).
inline double lfoUnit (int shape, double phaseCycles, float phaseOffset, bool unipolar)
{
    const double osc = lfoOsc (shape, phaseCycles + (double) phaseOffset);   // [-1, 1]
    return unipolar ? (osc * 0.5 + 0.5) : osc;                               // [0, 1] or [-1, 1]
}
