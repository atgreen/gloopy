// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <algorithm>

// Tempo-synced LFO rate for the modulation effects (Chorus/Flanger/Phaser). syncBeats is
// the LFO cycle length in beats: >0 gives a rate of bpm/(60*syncBeats) Hz (one cycle per
// syncBeats beats), clamped to [0.01, 20] Hz; syncBeats == 0 falls back to the free rate
// (clamped to the effects' [0.05, 8] Hz range). Factored out so it's unit-testable without
// the audio engine, and shared by the three effects (they call it with their `bpm` from
// Effect::setTempo). Pure; no JUCE dependency.
inline float effectSyncedRate (double bpm, float syncBeats, float freeRateHz)
{
    if (syncBeats > 0.0f)
    {
        const float r = (float) (bpm / (60.0 * (double) syncBeats));
        return std::min (20.0f, std::max (0.01f, r));
    }
    return std::min (8.0f, std::max (0.05f, freeRateHz));
}
