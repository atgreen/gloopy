// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
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

/** Strum: within each cluster of notes that share a start beat (a chord), stagger the
    starts so the voices fan out like a guitar strum. `stepBeats` is the delay between
    consecutive voices; `down` fans high→low (downstroke), else low→high (upstroke). */
inline void strumNotes (std::vector<Note>& notes, double stepBeats, bool down)
{
    if (stepBeats <= 0.0 || notes.size() < 2) return;
    std::map<long long, std::vector<int>> clusters;           // key = start beat (ms-quantised)
    for (int i = 0; i < (int) notes.size(); ++i)
        clusters[(long long) std::llround (notes[(size_t) i].startBeat * 1000.0)].push_back (i);

    for (auto& cl : clusters)
    {
        auto& idx = cl.second;
        if (idx.size() < 2) continue;                         // single note: nothing to strum
        std::sort (idx.begin(), idx.end(), [&] (int a, int b)
        {
            return down ? notes[(size_t) a].pitch > notes[(size_t) b].pitch
                        : notes[(size_t) a].pitch < notes[(size_t) b].pitch;
        });
        const double base = notes[(size_t) idx[0]].startBeat;
        for (int k = 0; k < (int) idx.size(); ++k)
            notes[(size_t) idx[k]].startBeat = juce::jmax (0.0, base + k * stepBeats);
    }
}

/** Arpeggiate: turn each chord (notes sharing a start beat) into a sequence of single
    notes, each `stepBeats` long, played in order. mode 0 = up, 1 = down, 2 = up-down.
    Single notes pass through unchanged. Note lengths become the step (classic arp gate). */
inline void arpeggiateNotes (std::vector<Note>& notes, double stepBeats, int mode)
{
    if (stepBeats <= 0.0 || notes.empty()) return;
    std::map<long long, std::vector<int>> clusters;           // key = start beat (ms-quantised)
    for (int i = 0; i < (int) notes.size(); ++i)
        clusters[(long long) std::llround (notes[(size_t) i].startBeat * 1000.0)].push_back (i);

    std::vector<Note> out;
    for (auto& cl : clusters)                                  // std::map → ascending start order
    {
        auto& idx = cl.second;
        if (idx.size() < 2) { out.push_back (notes[(size_t) idx[0]]); continue; }

        std::vector<int> pitches;
        for (int i : idx) pitches.push_back (notes[(size_t) i].pitch);
        std::sort (pitches.begin(), pitches.end());            // ascending

        std::vector<int> seq;
        if (mode == 1)                                         // down
            for (auto it = pitches.rbegin(); it != pitches.rend(); ++it) seq.push_back (*it);
        else if (mode == 2)                                    // up-down (don't repeat the ends)
        {
            seq = pitches;
            for (int k = (int) pitches.size() - 2; k >= 1; --k) seq.push_back (pitches[(size_t) k]);
        }
        else seq = pitches;                                    // up

        const double base = notes[(size_t) idx[0]].startBeat;
        const float  vel  = notes[(size_t) idx[0]].velocity;
        for (int k = 0; k < (int) seq.size(); ++k)
            out.push_back ({ seq[(size_t) k], juce::jmax (0.0, base + k * stepBeats),
                             juce::jmax (0.0625, stepBeats), juce::jlimit (0.0f, 1.0f, vel) });
    }
    notes = std::move (out);
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
