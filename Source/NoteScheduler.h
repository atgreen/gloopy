// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "Note.h"

/** Appends note-on/off events for the notes that fall within
    [loopPosStart, loopPosStart + chunk) to @p midi, timestamped as
    tsOffset + (eventSample - loopPosStart). Shared by every channel's playback. */
/** Swing: 0.5 = straight; >0.5 moves each beat's off-8th later (0.66 ≈ triplet). */
inline double swingBeat (double beat, double swing)
{
    if (swing <= 0.5) return beat;
    swing = juce::jlimit (0.5, 0.9, swing);
    const double ib = std::floor (beat), frac = beat - ib;
    const double sf = frac <= 0.5 ? frac * (swing / 0.5)
                                  : swing + (frac - 0.5) * ((1.0 - swing) / 0.5);
    return ib + sf;
}

inline void collectNotes (const std::vector<Note>& notes, juce::MidiBuffer& midi,
                          juce::int64 loopPosStart, int chunk, int tsOffset,
                          double samplesPerBeat, double swing = 0.5)
{
    const juce::int64 loopPosEnd = loopPosStart + chunk;

    for (const auto& n : notes)
    {
        const double startSw = swingBeat (n.startBeat, swing);
        const juce::int64 on  = (juce::int64) std::llround (startSw * samplesPerBeat);
        const juce::int64 off = (juce::int64) std::llround ((startSw + n.lengthBeats) * samplesPerBeat);

        if (on >= loopPosStart && on < loopPosEnd)
            midi.addEvent (juce::MidiMessage::noteOn (1, n.pitch, n.velocity),
                           tsOffset + (int) (on - loopPosStart));

        if (off >= loopPosStart && off < loopPosEnd)
            midi.addEvent (juce::MidiMessage::noteOff (1, n.pitch),
                           tsOffset + (int) (off - loopPosStart));
    }
}
