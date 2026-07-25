// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "Note.h"

// Pure piano-roll note transforms, shared by the control API (apiQuantizeClip etc.,
// ClipOps.cpp) and the PianoRoll UI so both do exactly the same edit.

/** Snap each note's start to the nearest multiple of `grid` beats (e.g. 0.25 = 16ths). */
inline void quantizeNotes (std::vector<Note>& notes, double grid)
{
    if (grid <= 0.0) return;
    for (auto& n : notes)
        n.startBeat = juce::jmax (0.0, std::round (n.startBeat / grid) * grid);
}

/** Shift every note by `semitones`, clamped to the MIDI range. */
inline void transposeNotes (std::vector<Note>& notes, int semitones)
{
    for (auto& n : notes)
        n.pitch = juce::jlimit (0, 127, n.pitch + semitones);
}

/** Add random timing (beats) and velocity (0..1) jitter, +/- the given amounts. */
inline void humanizeNotes (std::vector<Note>& notes, double timing, double velocity, juce::Random& rng)
{
    for (auto& n : notes)
    {
        if (timing > 0.0)   n.startBeat = juce::jmax (0.0, n.startBeat + (rng.nextDouble() * 2.0 - 1.0) * timing);
        if (velocity > 0.0) n.velocity  = juce::jlimit (0.0f, 1.0f, n.velocity + (float) ((rng.nextDouble() * 2.0 - 1.0) * velocity));
    }
}
