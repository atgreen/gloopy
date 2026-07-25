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

/** Knife: cut every note that spans `beat` (clip-relative) into two abutting notes —
    [start, beat) and [beat, end) — keeping pitch and velocity. Notes that merely start
    or end at the beat, or don't cross it, are untouched. A vertical cut through the roll. */
inline void splitNotesAtBeat (std::vector<Note>& notes, double beat)
{
    constexpr double eps = 1.0e-6;
    std::vector<Note> out;
    out.reserve (notes.size() + 4);
    for (const auto& n : notes)
    {
        const double end = n.startBeat + n.lengthBeats;
        if (n.startBeat + eps < beat && beat < end - eps)   // strictly inside the note
        {
            Note left = n;  left.lengthBeats  = beat - n.startBeat;
            Note right = n; right.startBeat = beat; right.lengthBeats = end - beat;
            out.push_back (left);
            out.push_back (right);
        }
        else
            out.push_back (n);
    }
    notes = std::move (out);
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

/** Live-arpeggiator expansion (non-destructive: computed from a clip's raw notes, played
    instead of them while the arp is on). Models a real DAW / hardware arpeggiator: walk the
    clip in `rateBeats` steps and, at each step, play the next note of the set *currently
    held* (sounding) at that step. So a held chord arpeggiates, a single sustained note
    repeats, and overlapping notes are picked up too — matching Ableton/Logic/hardware.
    `octaves` spans the pattern upward (1 = as-is), `gate` (0..1) is note length as a
    fraction of the step, mode: 0 up, 1 down, 2 up-down, 3 random. `swing` (0 = straight,
    up to ~0.9) delays every other grid step for a shuffle feel. `hold` latches the last
    sounding set across rests and, together with `holdLenBeats` > 0, keeps arpeggiating to
    that length (fill the whole clip after the keys release). Pure + deterministic (seeded),
    so it unit-tests and renders stably. */
inline std::vector<Note> expandArp (const std::vector<Note>& notes, double rateBeats,
                                    int octaves, float gate, int mode,
                                    float swing = 0.0f, bool hold = false, double holdLenBeats = 0.0)
{
    if (rateBeats <= 0.0 || notes.empty()) return notes;
    const int   octs = juce::jlimit (1, 6, octaves);
    const float g    = juce::jlimit (0.05f, 1.0f, gate);
    const float sw   = juce::jlimit (0.0f, 0.9f, swing);
    const double eps = 1e-6;

    double tStart = notes[0].startBeat, tEnd = notes[0].startBeat;
    for (const auto& n : notes)
    {
        tStart = juce::jmin (tStart, n.startBeat);
        tEnd   = juce::jmax (tEnd, n.startBeat + n.lengthBeats);
    }
    if (hold && holdLenBeats > 0.0) tEnd = juce::jmax (tEnd, tStart + holdLenBeats);
    if (tEnd <= tStart + eps) return notes;

    juce::Random rng ((juce::int64) std::llround (tStart * 1000.0) + (juce::int64) notes.size() * 131 + 17);
    std::vector<Note> out;
    std::vector<int> lastHeld; float lastVel = 0.8f;           // for hold/latch across rests
    int stepIndex = 0;

    for (double t = tStart; t < tEnd - eps; t += rateBeats)
    {
        // Notes sounding at this step's start.
        std::vector<int> held; float vel = 0.0f;
        for (const auto& n : notes)
            if (n.startBeat <= t + eps && t + eps < n.startBeat + n.lengthBeats)
                { held.push_back (n.pitch); vel = juce::jmax (vel, n.velocity); }
        std::sort (held.begin(), held.end());
        held.erase (std::unique (held.begin(), held.end()), held.end());

        if (held.empty())
        {
            if (hold && ! lastHeld.empty()) { held = lastHeld; vel = lastVel; }   // latch the last set
            else continue;                                     // a rest
        }
        else { lastHeld = held; lastVel = vel; }

        std::vector<int> pat;                                  // held set across octaves
        for (int o = 0; o < octs; ++o)
            for (int p : held) pat.push_back (juce::jlimit (0, 127, p + 12 * o));

        std::vector<int> seq;                                  // ordered by mode
        if (mode == 1)      for (auto it = pat.rbegin(); it != pat.rend(); ++it) seq.push_back (*it);
        else if (mode == 2) { seq = pat; for (int k = (int) pat.size() - 2; k >= 1; --k) seq.push_back (pat[(size_t) k]); }
        else                seq = pat;                         // up (random also draws from this)
        if (seq.empty()) continue;

        const int pitch = (mode == 3) ? seq[(size_t) rng.nextInt ((int) seq.size())]
                                      : seq[(size_t) (stepIndex % (int) seq.size())];
        // Swing: push odd grid steps later (aligned to the grid, so rests don't desync it).
        const long long gridStep = std::llround ((t - tStart) / rateBeats);
        const double swOff = (sw > 0.0f && (gridStep % 2 != 0)) ? (double) sw * 0.5 * rateBeats : 0.0;
        out.push_back ({ pitch, t + swOff, rateBeats * g, juce::jlimit (0.0f, 1.0f, vel > 0.0f ? vel : 0.8f) });
        ++stepIndex;
    }
    return out.empty() ? notes : out;
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
