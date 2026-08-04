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
// Deterministic per-note-per-repetition gate for note probability. A fixed integer bit-mix
// of (pitch, quantised note start, quantised repetition start) -> [0,1); the note fires when
// that is below its probability. Deterministic => renders are reproducible, yet each looped
// repetition rolls independently. (Same hash family as Lfo.h's sample-and-hold.)
inline bool noteFires (int pitch, double startBeat, double repStartBeat, float probability)
{
    if (probability >= 1.0f) return true;
    if (probability <= 0.0f) return false;
    unsigned long long h = (unsigned long long) (pitch * 2654435761u)
                         ^ ((unsigned long long) std::llround (startBeat    * 960.0) * 0x9E3779B97F4A7C15ULL)
                         ^ ((unsigned long long) std::llround (repStartBeat * 960.0) * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL; h ^= h >> 27; h *= 0x94D049BB133111EBULL; h ^= h >> 31;
    return (double) (h >> 11) / (double) (1ULL << 53) < (double) probability;   // [0,1) < p
}

inline void collectNotes (const std::vector<Note>& notes, juce::MidiBuffer& midi,
                          const TempoConv& tc, double repStartBeat,
                          juce::int64 songStart, int tsOffset,
                          juce::int64 winLo, juce::int64 winHi, double swing = 0.5,
                          int transpose = 0, float velocityScale = 1.0f)
{
    for (const auto& n : notes)
    {
        if (! noteFires (n.pitch, n.startBeat.toBeats(), repStartBeat, n.probability)) continue;   // generative gate
        const double startSw = swingBeat (n.startBeat.toBeats(), swing);
        const juce::int64 on  = tc.beatToSample (repStartBeat + startSw);
        const juce::int64 off = tc.beatToSample (repStartBeat + startSw + n.lengthBeats.toBeats());
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

/** Session-view clip playback: emit a launched session clip's notes for the block sub-range
    [firstSample, firstSample + count), phased from `launchBeat` in the monotonic *session-beat*
    space and looping over `loopLenBeats`. Unlike collectClip (which anchors a clip on the
    arrangement timeline), a session clip has no timeline position — it loops from where it was
    launched. Constant tempo: session clips play at the current samples-per-beat (`spb`); they
    don't follow tempo-map changes (a v1 simplification — pass a constant-path TempoConv built
    with `spb`). Events land at block-sample offsets via the same songStart-reference trick as
    collectNotes. See docs/session-view.md. */
inline void collectSessionClip (const std::vector<Note>& notes, juce::MidiBuffer& midi,
                                const TempoConv& tc, double spb,
                                double launchBeat, double blockStartSessionBeat,
                                int firstSample, int count,
                                double loopLenBeats, int transpose = 0, float velocityScale = 1.0f)
{
    if (count <= 0 || loopLenBeats <= 0.0 || spb <= 0.0) return;

    // Session-beat span of this sub-range, and the sample references collectNotes needs so its
    // timestamps come out as block-sample offsets (songStart maps blockStartSessionBeat -> 0).
    const double      aBeat     = blockStartSessionBeat + (double) firstSample / spb;
    const double      bBeat     = blockStartSessionBeat + (double) (firstSample + count) / spb;
    const juce::int64 songStart = tc.beatToSample (blockStartSessionBeat);
    const juce::int64 winLo     = tc.beatToSample (aBeat);
    const juce::int64 winHi     = tc.beatToSample (bBeat);
    if (winLo >= winHi) return;

    // Walk the loop repetitions that overlap the sub-range; each repetition anchors the clip's
    // notes at launchBeat + k * loopLen (monotonic, so it never wraps — only the anchor tiles).
    double elapsedA = aBeat - launchBeat;
    if (elapsedA < 0.0) elapsedA = 0.0;                        // not started yet (shouldn't occur post-launch)
    int repK = (int) std::floor (elapsedA / loopLenBeats);
    if (repK < 0) repK = 0;
    for (int guard = 0; guard < 100000; ++guard, ++repK)
    {
        const double repStartBeat = launchBeat + (double) repK * loopLenBeats;
        if (tc.beatToSample (repStartBeat) >= winHi) break;
        collectNotes (notes, midi, tc, repStartBeat, songStart, /*tsOffset*/ 0,
                      winLo, winHi, /*swing (straight)*/ 0.5, transpose, velocityScale);
    }
}
