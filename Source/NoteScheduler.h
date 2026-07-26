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

// Allocation-free beat<->sample converter, snapshotted once per block on the audio
// thread. An empty tempo map takes the *constant* path — llround(beat*spb), byte-
// identical to the pre-tempo-map scheduler. A non-empty map integrates the tempo
// piecewise over a fixed-size marker snapshot (mirroring Tempo.cpp's
// apiBeatsToSeconds / apiSecondsToBeats), so renderBlock never locks or touches the
// heap to convert. See grind Wave 4 #10.
struct TempoConv
{
    static constexpr int kMaxMarkers = 256;
    bool   constant  = true;
    double spb        = 0.0;    // constant path: samples per beat
    double sampleRate = 0.0;
    int    n = 0;
    double beatAt [kMaxMarkers] {};
    double bpmAt  [kMaxMarkers] {};
    double secAt  [kMaxMarkers] {};   // cumulative seconds from beat 0 to beatAt[i]

    // Populate from a beat-ascending marker snapshot. Empty / over-capacity / bad rate
    // -> constant path at @p fallbackSpb (so behaviour is unchanged with no tempo map).
    void setMarkers (const double* beats, const double* bpms, int count,
                     double rate, double fallbackSpb)
    {
        if (count <= 0 || rate <= 0.0) { constant = true; spb = fallbackSpb; return; }
        constant = false; sampleRate = rate;
        n = juce::jmin (count, kMaxMarkers);
        for (int i = 0; i < n; ++i) { beatAt[i] = beats[i]; bpmAt[i] = juce::jmax (1.0, bpms[i]); }
        secAt[0] = beatAt[0] * 60.0 / bpmAt[0];               // before-first segment at first bpm
        for (int i = 1; i < n; ++i)
            secAt[i] = secAt[i - 1] + (beatAt[i] - beatAt[i - 1]) * 60.0 / bpmAt[i - 1];
    }

    juce::int64 beatToSample (double beat) const
    {
        if (constant) return (juce::int64) std::llround (beat * spb);
        double sec;
        if (beat <= beatAt[0]) sec = beat * 60.0 / bpmAt[0];
        else
        {
            int i = 0;
            while (i + 1 < n && beatAt[i + 1] <= beat) ++i;
            sec = secAt[i] + (beat - beatAt[i]) * 60.0 / bpmAt[i];
        }
        return (juce::int64) std::llround (sec * sampleRate);
    }

    double sampleToBeat (juce::int64 s) const
    {
        if (constant) return spb > 0.0 ? (double) s / spb : 0.0;
        const double sec = (double) s / sampleRate;
        if (sec <= secAt[0]) return sec * bpmAt[0] / 60.0;
        int i = 0;
        while (i + 1 < n && secAt[i + 1] <= sec) ++i;
        return beatAt[i] + (sec - secAt[i]) * bpmAt[i] / 60.0;
    }
};

// Append note-on/off events for @p notes (one clip repetition, whose window in the
// song timeline begins at absolute beat @p repStartBeat) that fall within the song-
// sample window [winLo, winHi). Each note is placed at its *absolute* song-sample
// position via @p tc, so per-repetition tempo variation is honoured; timestamps are
// tsOffset + (sample - songStart). Half-open on both endpoints, matching the loop-
// window semantics (a note-off past the window is dropped, as before).
inline void collectNotes (const std::vector<Note>& notes, juce::MidiBuffer& midi,
                          const TempoConv& tc, double repStartBeat,
                          juce::int64 songStart, int tsOffset,
                          juce::int64 winLo, juce::int64 winHi, double swing = 0.5,
                          int transpose = 0, float velocityScale = 1.0f)
{
    for (const auto& n : notes)
    {
        const double startSw = swingBeat (n.startBeat, swing);
        const juce::int64 on  = tc.beatToSample (repStartBeat + startSw);
        const juce::int64 off = tc.beatToSample (repStartBeat + startSw + n.lengthBeats);
        const int pitch = juce::jlimit (0, 127, n.pitch + transpose);   // non-destructive clip transpose
        const float vel = juce::jlimit (0.0f, 1.0f, n.velocity * velocityScale);   // non-destructive clip velocity scale

        if (on >= winLo && on < winHi)
            midi.addEvent (juce::MidiMessage::noteOn (1, pitch, vel),
                           tsOffset + (int) (on - songStart));

        if (off >= winLo && off < winHi)
            midi.addEvent (juce::MidiMessage::noteOff (1, pitch),
                           tsOffset + (int) (off - songStart));
    }
}
