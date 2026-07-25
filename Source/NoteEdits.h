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

/** Semitone offsets for a chord type (falls back to a major triad). */
inline std::vector<int> chordIntervals (const juce::String& type)
{
    const auto t = type.toLowerCase().trim();
    if (t == "min"  || t == "minor" || t == "m")   return { 0, 3, 7 };
    if (t == "dim")                                return { 0, 3, 6 };
    if (t == "aug")                                return { 0, 4, 8 };
    if (t == "sus2")                               return { 0, 2, 7 };
    if (t == "sus4")                               return { 0, 5, 7 };
    if (t == "6")                                  return { 0, 4, 7, 9 };
    if (t == "min6" || t == "m6")                  return { 0, 3, 7, 9 };
    if (t == "7"    || t == "dom7")                return { 0, 4, 7, 10 };
    if (t == "maj7" || t == "major7")              return { 0, 4, 7, 11 };
    if (t == "min7" || t == "m7")                  return { 0, 3, 7, 10 };
    if (t == "m7b5" || t == "halfdim")             return { 0, 3, 6, 10 };
    if (t == "dim7")                               return { 0, 3, 6, 9 };
    if (t == "add9")                               return { 0, 4, 7, 14 };
    if (t == "maj9")                               return { 0, 4, 7, 11, 14 };
    if (t == "min9" || t == "m9")                  return { 0, 3, 7, 10, 14 };
    if (t == "5"    || t == "power")               return { 0, 7 };
    return { 0, 4, 7 };   // major triad
}

/** Build a chord's notes at a beat position. `inversion` raises the lowest N notes an octave. */
inline std::vector<Note> makeChord (int root, const juce::String& type, int inversion,
                                    double startBeat, double lengthBeats, float velocity)
{
    auto iv = chordIntervals (type);
    for (int k = 0; k < inversion && ! iv.empty(); ++k)
        iv[(size_t) (k % (int) iv.size())] += 12;   // rotate lowest voices up
    std::vector<Note> out;
    for (int i : iv)
        out.push_back ({ juce::jlimit (0, 127, root + i), juce::jmax (0.0, startBeat),
                         juce::jmax (0.0625, lengthBeats), juce::jlimit (0.0f, 1.0f, velocity) });
    return out;
}
