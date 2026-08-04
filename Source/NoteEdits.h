// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Note.h"
#include "Rational.h"

// Pure piano-roll note transforms, shared by the control API (apiQuantizeClip etc.,
// ClipOps.cpp) and the PianoRoll UI so both do exactly the same edit.
//
// Note positions are exact rationals (Note::startBeat / lengthBeats are BeatRatio). These
// transforms read a position with .toBeats() (a double at the math boundary) and assign the
// result straight back — the implicit double→BeatRatio ctor snaps it to the exact grid, so
// positions stay drift-free. Quantize is the exception: it snaps on exact rational grid math.

/** Snap each note's start toward the nearest multiple of `grid` beats (e.g. 0.25 = 16ths).
    `strength` 0..1 is how far to move it: 1 = full snap (the default), 0.5 = halfway to the
    grid (tighten without robotizing — the classic "iterative"/partial quantize), 0 = no move. */
inline void quantizeNotes (std::vector<Note>& notes, double grid, double strength = 1.0)
{
    if (grid <= 0.0) return;
    const double s = juce::jlimit (0.0, 1.0, strength);
    // Snap on EXACT rational grid math (radium #2): the target grid line is k · grid computed as a
    // reduced fraction, so a triplet grid (1/3) lands a note on exactly 2/3 or 1.0 instead of
    // 0.6666…/0.9999…. Repeated full-strength quantizes are then idempotent — no accumulating drift.
    const auto gridR = gloopy::time::BeatRatio::fromBeats (grid);
    if (gridR.isZero()) return;
    for (auto& n : notes)
    {
        const double sb     = n.startBeat.toBeats();
        const auto   k      = (std::int64_t) std::llround (sb / grid);   // nearest grid line
        const double target = (gridR * k).toBeats();                     // exact k·grid
        n.startBeat = juce::jmax (0.0, s >= 1.0 ? target : sb + (target - sb) * s);
    }
}

/** Swing / groove: delay every off-beat note by `amount` of a `grid` step, baking a shuffle
    into the clip. A note sitting on an ODD grid line (the &-of-the-beat at that resolution —
    e.g. grid 0.5 = the off-8ths) is pushed later by `amount·grid`; on-beat notes stay put.
    `amount` 0 = straight, ~0.33 = a triplet shuffle, up to 0.9. Length and pitch/velocity are
    preserved (the onset moves). Distinct from the *live* transport swing (global, non-baked) —
    this writes the groove into the notes, so a clip can carry its own feel and export it. */
inline void swingNotes (std::vector<Note>& notes, double grid, float amount)
{
    if (grid <= 0.0) return;
    const double amt   = juce::jlimit (0.0, 0.9, (double) amount);
    const double delay = amt * grid;
    for (auto& n : notes)
    {
        const double pos = n.startBeat.toBeats() / grid;
        const long   idx = (long) std::llround (pos);
        if (std::abs (pos - (double) idx) < 1e-6 && (idx & 1L))   // on an odd grid line -> off-beat
            n.startBeat = n.startBeat.toBeats() + delay;
    }
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
        if (timing > 0.0)   n.startBeat = juce::jmax (0.0, n.startBeat.toBeats() + (rng.nextDouble() * 2.0 - 1.0) * timing);
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
        const double sb  = n.startBeat.toBeats();
        const double end = sb + n.lengthBeats.toBeats();
        if (sb + eps < beat && beat < end - eps)   // strictly inside the note
        {
            Note left = n;  left.lengthBeats  = beat - sb;
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
        clusters[(long long) std::llround (notes[(size_t) i].startBeat.toBeats() * 1000.0)].push_back (i);

    for (auto& cl : clusters)
    {
        auto& idx = cl.second;
        if (idx.size() < 2) continue;                         // single note: nothing to strum
        std::sort (idx.begin(), idx.end(), [&] (int a, int b)
        {
            return down ? notes[(size_t) a].pitch > notes[(size_t) b].pitch
                        : notes[(size_t) a].pitch < notes[(size_t) b].pitch;
        });
        const double base = notes[(size_t) idx[0]].startBeat.toBeats();
        for (int k = 0; k < (int) idx.size(); ++k)
            notes[(size_t) idx[k]].startBeat = juce::jmax (0.0, base + k * stepBeats);
    }
}

/** Legato: stretch (or shrink) each note's length so its end reaches the NEXT distinct
    onset in the clip, blended by `amount` (0 = unchanged, 1 = notes exactly touch the next
    onset). Notes sharing the last onset keep their length. Chords are handled by using the
    next *distinct* start beat, so a whole chord extends to the next event together. Pure,
    size- and order-preserving (so the piano-roll selection write-back works). */
inline void legatoNotes (std::vector<Note>& notes, float amount)
{
    if (notes.size() < 2) return;
    const double a = juce::jlimit (0.0f, 1.0f, amount);
    std::vector<double> starts;                            // distinct onsets, ascending
    for (const auto& n : notes) starts.push_back (n.startBeat.toBeats());
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end(),
                    [] (double x, double y) { return std::abs (x - y) < 1e-6; }), starts.end());
    for (auto& n : notes)
    {
        const double sb = n.startBeat.toBeats();
        double next = -1.0;                                // next onset strictly after this note
        for (double s : starts) if (s > sb + 1e-6) { next = s; break; }
        if (next < 0.0) continue;                          // last onset group: length unchanged
        const double target = next - sb;                   // length that just reaches the next onset
        n.lengthBeats = juce::jmax (0.01, n.lengthBeats.toBeats() + a * (target - n.lengthBeats.toBeats()));
    }
}

/** Velocity ramp (crescendo / decrescendo): linearly interpolate each note's velocity
    from `fromVel` at the first onset to `toVel` at the last onset, by the note's start-beat
    position. A crescendo passes from<to, a decrescendo from>to. Notes sharing an onset get
    the same velocity; a single onset gets `toVel`. Pure, size- and order-preserving. */
inline void rampVelocities (std::vector<Note>& notes, float fromVel, float toVel)
{
    if (notes.empty()) return;
    const float a = juce::jlimit (0.0f, 1.0f, fromVel), b = juce::jlimit (0.0f, 1.0f, toVel);
    double lo = notes[0].startBeat.toBeats(), hi = notes[0].startBeat.toBeats();
    for (const auto& n : notes) { lo = juce::jmin (lo, n.startBeat.toBeats()); hi = juce::jmax (hi, n.startBeat.toBeats()); }
    const double span = hi - lo;
    for (auto& n : notes)
    {
        const double t = span > 1e-9 ? (n.startBeat.toBeats() - lo) / span : 1.0;
        n.velocity = juce::jlimit (0.0f, 1.0f, a + (float) t * (b - a));
    }
}

/** Flatten dynamics: set every note's velocity to a single `value` (0..1) — make a part
    mechanically even, or reset dynamics before re-shaping. Distinct from velocity-scale
    (multiply), ramp (gradient) and humanize (jitter): flatten makes them all identical.
    Pure, size- and order-preserving. */
inline void flattenVelocities (std::vector<Note>& notes, float value)
{
    const float v = juce::jlimit (0.0f, 1.0f, value);
    for (auto& n : notes) n.velocity = v;
}

/** Time-scale: multiply every note's start and length by `factor` (0.5 = double-time /
    twice as fast, 2 = half-time / twice as slow). Preserves rhythm proportions, pitch and
    velocity; size- and order-preserving. The caller scales the clip's content/length bounds
    to match. */
inline void scaleNoteTimes (std::vector<Note>& notes, double factor)
{
    const double f = juce::jlimit (0.125, 8.0, factor);
    for (auto& n : notes) { n.startBeat = n.startBeat.toBeats() * f; n.lengthBeats = juce::jmax (0.01, n.lengthBeats.toBeats() * f); }
}

/** Gate / articulation: scale every note's LENGTH by `factor`, keeping its start (so the
    rhythm is unchanged) — 0.5 = staccato (detached), 1.5 = tenuto (fuller). Distinct from
    legato (which stretches each note to the NEXT onset) and from time-scale (which scales
    start AND length): gate reshapes only articulation. Length floored at 0.01. */
inline void gateNotes (std::vector<Note>& notes, double factor)
{
    const double f = juce::jlimit (0.05, 8.0, factor);
    for (auto& n : notes) n.lengthBeats = juce::jmax (0.01, n.lengthBeats.toBeats() * f);
}

/** Ratchet / roll: subdivide every note into `subdivisions` equal same-pitch hits filling
    its original span (a drum roll / stutter). Each hit keeps the note's pitch and velocity;
    its length is the subdivided step. Distinct from arpeggiate (which sequences a chord's
    different pitches) — ratchet re-triggers the SAME note. Size-changing. */
inline void ratchetNotes (std::vector<Note>& notes, int subdivisions)
{
    const int n = juce::jlimit (2, 16, subdivisions);
    std::vector<Note> out;
    for (const auto& note : notes)
    {
        const double base = note.startBeat.toBeats();
        const double step = note.lengthBeats.toBeats() / n;
        for (int k = 0; k < n; ++k)
        {
            Note h = note;
            h.startBeat   = base + (double) k * step;
            h.lengthBeats = juce::jmax (0.01, step);
            out.push_back (h);
        }
    }
    notes = std::move (out);
}

/** Melodic inversion: mirror every note's pitch around a pivot (the earliest-starting
    note's pitch), so intervals flip direction — an ascending line becomes descending. The
    pivot note stays put; `newPitch = 2·pivot − pitch`, clamped to 0..127. Timing, length and
    velocity are preserved; size- and order-preserving. A core compositional device. */
inline void invertNotes (std::vector<Note>& notes)
{
    if (notes.empty()) return;
    int pivot = notes[0].pitch; double earliest = notes[0].startBeat.toBeats();
    for (const auto& n : notes) if (n.startBeat.toBeats() < earliest) { earliest = n.startBeat.toBeats(); pivot = n.pitch; }
    for (auto& n : notes) n.pitch = juce::jlimit (0, 127, 2 * pivot - n.pitch);
}

/** Harmonize: for every note, add a parallel voice `semitones` away (a fixed interval —
    +7 a fifth, +12 an octave, -12 an octave down, etc.), keeping the original. The added
    voice shares the note's start, length and velocity; its pitch is clamped to 0..127 (a
    harmony that would fall off the keyboard is dropped, not folded). The originals stay, so
    a monophonic line becomes parallel harmony. Distinct from transpose (which *moves* every
    note) and invert (which mirrors) — harmonize *thickens*. Size-changing (grows the list). */
inline void harmonizeNotes (std::vector<Note>& notes, int semitones)
{
    if (semitones == 0) return;                              // nothing to add
    const std::vector<Note> src (notes);                    // snapshot (we append)
    for (const auto& n : src)
    {
        const int p = n.pitch + semitones;
        if (p < 0 || p > 127) continue;                     // off the keyboard — drop this voice
        Note h = n;
        h.pitch = p;
        notes.push_back (h);
    }
}

/** Chordify: turn every note into a full chord by adding a voice at each interval in
    `intervals` (semitones above the note — the root stays as the played note). E.g. a major
    triad is {4,7}, a dominant 7th {4,7,10}. Added voices share the note's start/length/
    velocity; a voice off the 0..127 keyboard is dropped. Generalises harmonize (one interval)
    to a named chord. Size-changing; the originals are kept as the chord roots. */
inline void chordifyNotes (std::vector<Note>& notes, const std::vector<int>& intervals)
{
    if (intervals.empty()) return;
    const std::vector<Note> src (notes);                    // snapshot (we append)
    for (const auto& n : src)
        for (const int iv : intervals)
        {
            if (iv == 0) continue;                          // 0 = the root, already present
            const int p = n.pitch + iv;
            if (p < 0 || p > 127) continue;                 // off the keyboard — drop this voice
            Note h = n;
            h.pitch = p;
            notes.push_back (h);
        }
}

/** MIDI echo / delay: append `repeats` decaying copies of every note, each `delayBeats`
    later than the last, with velocity multiplied by `feedback` each step (copies that fade
    below ~1% are dropped). The originals are kept; pitch and length are preserved. A
    generative note *multiplier* (grows the list), distinct from the shaping transforms. */
inline void echoNotes (std::vector<Note>& notes, double delayBeats, int repeats, float feedback)
{
    if (delayBeats <= 0.0 || repeats <= 0) return;
    const int   reps = juce::jlimit (1, 16, repeats);
    const float fb   = juce::jlimit (0.0f, 1.0f, feedback);
    const std::vector<Note> src (notes);                    // snapshot the originals (we append)
    for (const auto& n : src)
    {
        const double sb = n.startBeat.toBeats();
        float vel = n.velocity;
        for (int k = 1; k <= reps; ++k)
        {
            vel *= fb;
            if (vel < 0.01f) break;                         // faded out
            Note e = n;
            e.startBeat = sb + (double) k * delayBeats;
            e.velocity  = juce::jlimit (0.0f, 1.0f, vel);
            notes.push_back (e);
        }
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
        clusters[(long long) std::llround (notes[(size_t) i].startBeat.toBeats() * 1000.0)].push_back (i);

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

        const double base = notes[(size_t) idx[0]].startBeat.toBeats();
        const float  vel  = notes[(size_t) idx[0]].velocity;
        for (int k = 0; k < (int) seq.size(); ++k)
            out.push_back ({ seq[(size_t) k], juce::jmax (0.0, base + k * stepBeats),
                             juce::jmax (0.0625, stepBeats), juce::jlimit (0.0f, 1.0f, vel) });
    }
    notes = std::move (out);
}

/** The ordered pitch sequence an arpeggiator plays over one held set: the sorted held pitches
    repeated across `octaves` (each +12), then ordered by `mode` (0 up, 1 down, 2 up-down w/o
    repeating the endpoints, 3 random draws from the up order). Shared by the clip arp
    (expandArp) and the live arp (LiveArp) so they behave identically. */
inline std::vector<int> arpSequence (const std::vector<int>& heldSorted, int octaves, int mode)
{
    const int octs = juce::jlimit (1, 6, octaves);
    std::vector<int> pat;
    for (int o = 0; o < octs; ++o)
        for (int p : heldSorted) pat.push_back (juce::jlimit (0, 127, p + 12 * o));
    std::vector<int> seq;
    if (mode == 1)      for (auto it = pat.rbegin(); it != pat.rend(); ++it) seq.push_back (*it);
    else if (mode == 2) { seq = pat; for (int k = (int) pat.size() - 2; k >= 1; --k) seq.push_back (pat[(size_t) k]); }
    else                seq = pat;                             // up (random draws from this order)
    return seq;
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
                                    float swing = 0.0f, bool hold = false, double holdLenBeats = 0.0,
                                    float probability = 1.0f)
{
    if (rateBeats <= 0.0 || notes.empty()) return notes;
    const int   octs = juce::jlimit (1, 6, octaves);
    const float g    = juce::jlimit (0.05f, 1.0f, gate);
    const float sw   = juce::jlimit (0.0f, 0.9f, swing);
    const double eps = 1e-6;

    double tStart = notes[0].startBeat.toBeats(), tEnd = notes[0].startBeat.toBeats();
    for (const auto& n : notes)
    {
        tStart = juce::jmin (tStart, n.startBeat.toBeats());
        tEnd   = juce::jmax (tEnd, n.startBeat.toBeats() + n.lengthBeats.toBeats());
    }
    if (hold && holdLenBeats > 0.0) tEnd = juce::jmax (tEnd, tStart + holdLenBeats);
    if (tEnd <= tStart + eps) return notes;

    juce::Random rng ((juce::int64) std::llround (tStart * 1000.0) + (juce::int64) notes.size() * 131 + 17);
    std::vector<Note> out;
    std::vector<int> lastHeld; float lastVel = 0.8f;           // for hold/latch across rests
    std::vector<int> prevSet;                                  // last note-set the pattern ran over
    int stepIndex = 0;

    for (double t = tStart; t < tEnd - eps; t += rateBeats)
    {
        // Notes sounding at this step's start.
        std::vector<int> held; float vel = 0.0f;
        for (const auto& n : notes)
        {
            const double sb = n.startBeat.toBeats();
            if (sb <= t + eps && t + eps < sb + n.lengthBeats.toBeats())
                { held.push_back (n.pitch); vel = juce::jmax (vel, n.velocity); }
        }
        std::sort (held.begin(), held.end());
        held.erase (std::unique (held.begin(), held.end()), held.end());

        if (held.empty())
        {
            if (hold && ! lastHeld.empty()) { held = lastHeld; vel = lastVel; }   // latch the last set
            else continue;                                     // a rest
        }
        else { lastHeld = held; lastVel = vel; }

        // A new held set restarts the pattern from its first note (so each chord arpeggiates
        // from its root, matching hardware/Ableton/LMMS) — rather than carrying the running
        // index over and starting the new chord mid-pattern. Latched (held == prevSet) steps
        // keep advancing.
        if (held != prevSet) { stepIndex = 0; prevSet = held; }

        std::vector<int> seq = arpSequence (held, octs, mode);   // held set across octaves, by mode
        if (seq.empty()) continue;

        const int pitch = (mode == 3) ? seq[(size_t) rng.nextInt ((int) seq.size())]
                                      : seq[(size_t) (stepIndex % (int) seq.size())];
        // Swing: push odd grid steps later (aligned to the grid, so rests don't desync it).
        const long long gridStep = std::llround ((t - tStart) / rateBeats);
        const double swOff = (sw > 0.0f && (gridStep % 2 != 0)) ? (double) sw * 0.5 * rateBeats : 0.0;
        // The generative gate rides on Note.probability, so the shared deterministic
        // noteFires() in collectNotes drops steps reproducibly (per looped repetition).
        out.push_back ({ pitch, t + swOff, rateBeats * g,
                         juce::jlimit (0.0f, 1.0f, vel > 0.0f ? vel : 0.8f),
                         juce::jlimit (0.0f, 1.0f, probability) });
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
