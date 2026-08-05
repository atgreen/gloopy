// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

/** Meter map — an initial time signature plus mid-song time-signature changes.

    Positions are in quarter-note beats (the note model's unit). A signature's bar length in
    those beats is `num * 4 / den` (4/4 -> 4, 3/4 -> 3, 6/8 -> 3, 7/8 -> 3.5). Bar boundaries
    are found by walking segment by segment: the map is the initial signature from beat 0 until
    the first change, then each change's signature until the next. Changes are expected to fall
    on a bar boundary of the preceding segment (the editor snaps them there) so bar numbering
    stays clean; an off-grid change just starts a fresh (short) bar, which is harmless.

    The beat<->pixel mapping in the arrangement is meter-independent (a quarter note is always
    the same width); only bar-line positions and bar *numbering* come from here. Header-only and
    dependency-free so it can be unit-tested and used on the audio thread (see MeterConv). */
namespace gloopy::time
{

struct MeterChange { double beat; int num; int den; };

inline double barBeats (int num, int den) noexcept
{
    const int n = num > 0 ? num : 1;
    const int d = den > 0 ? den : 1;
    return (double) n * 4.0 / (double) d;
}

struct MeterMap
{
    int initNum { 4 };
    int initDen { 4 };
    std::vector<MeterChange> changes;   // beat-ascending; a change at beat 0 overrides the initial

    MeterMap() = default;
    MeterMap (int n, int d, std::vector<MeterChange> c = {})
        : initNum (n > 0 ? n : 4), initDen (d > 0 ? d : 4), changes (std::move (c))
    {
        std::sort (changes.begin(), changes.end(),
                   [] (const MeterChange& a, const MeterChange& b) { return a.beat < b.beat; });
    }

    // The signature segments: segment 0 at beat 0 with the initial meter, then one per change.
    // A change at beat 0 overrides the initial rather than adding a zero-length segment.
    std::vector<MeterChange> segments() const
    {
        std::vector<MeterChange> segs { { 0.0, initNum, initDen } };
        for (const auto& c : changes)
        {
            if (c.beat <= 1.0e-9) { segs.front().num = c.num; segs.front().den = c.den; }
            else                    segs.push_back (c);
        }
        return segs;
    }

    double beatsPerBarAt (double beat) const
    {
        double bpb = barBeats (initNum, initDen);
        for (const auto& c : changes) { if (c.beat <= beat + 1.0e-9) bpb = barBeats (c.num, c.den); else break; }
        return bpb;
    }

    void signatureAt (double beat, int& num, int& den) const
    {
        num = initNum; den = initDen;
        for (const auto& c : changes) { if (c.beat <= beat + 1.0e-9) { num = c.num; den = c.den; } else break; }
    }

    // Absolute beat -> (bar, beat-in-bar), both 1-based (matches the "bar . beat . tick" readout).
    void beatToBarBeat (double beat, int& bar, double& beatInBar) const
    {
        const double b = beat > 0.0 ? beat : 0.0;
        const auto segs = segments();
        int barCount = 0;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const double start = segs[i].beat;
            const double end   = (i + 1 < segs.size()) ? segs[i + 1].beat
                                                       : std::numeric_limits<double>::infinity();
            const double bpb   = barBeats (segs[i].num, segs[i].den);
            if (b < end - 1.0e-9 || ! std::isfinite (end))
            {
                const int barsInto = (int) std::floor ((b - start) / bpb + 1.0e-9);
                bar       = barCount + barsInto + 1;
                beatInBar = (b - start) - (double) barsInto * bpb + 1.0;
                return;
            }
            barCount += (int) std::llround ((end - start) / bpb);
        }
        bar = barCount + 1; beatInBar = 1.0;
    }

    // (bar, beat-in-bar), 1-based -> absolute beat.
    double barBeatToBeats (int bar, double beatInBar) const
    {
        const int target = (bar > 1 ? bar : 1) - 1;   // 0-based bar index
        const auto segs = segments();
        int barCount = 0;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const double start = segs[i].beat;
            const bool   last  = (i + 1 >= segs.size());
            const double bpb   = barBeats (segs[i].num, segs[i].den);
            const int    barsInSeg = last ? (target - barCount + 1)
                                          : (int) std::llround ((segs[i + 1].beat - start) / bpb);
            if (target < barCount + barsInSeg)
                return start + (double) (target - barCount) * bpb + (beatInBar - 1.0);
            barCount += barsInSeg;
        }
        return 0.0;
    }

    // The nearest bar line to a beat.
    double snapToBar (double beat) const
    {
        int bar; double bib;
        beatToBarBeat (beat, bar, bib);
        const double here = barBeatToBeats (bar, 1.0);
        const double next = barBeatToBeats (bar + 1, 1.0);
        return (beat - here <= next - beat) ? here : next;
    }

    // The start beat of the bar a given beat falls in (floor to the bar).
    double barStart (double beat) const
    {
        int bar; double bib;
        beatToBarBeat (beat, bar, bib);
        return barBeatToBeats (bar, 1.0);
    }

    // Invoke fn(barNumber1Based, beatAtBarStart) for every bar line in [0, endBeat].
    template <typename Fn>
    void forEachBarLine (double endBeat, Fn&& fn) const
    {
        const auto segs = segments();
        int bar = 1;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const double start = segs[i].beat;
            const double end   = (i + 1 < segs.size()) ? segs[i + 1].beat : (endBeat + 1.0);
            const double bpb   = barBeats (segs[i].num, segs[i].den);
            for (double x = start; x < end - 1.0e-6; x += bpb)
            {
                if (x > endBeat + 1.0e-6) return;
                fn (bar, x);
                ++bar;
            }
        }
    }
};

/** Allocation-free meter snapshot for the audio thread (mirrors NoteScheduler's TempoConv).
    Filled once per block from the sorted change list; queried per beat for downbeat accents. */
struct MeterConv
{
    static constexpr int kMaxSegs = 64;
    double segStart [kMaxSegs] { 0.0 };
    double segBpb   [kMaxSegs] { 4.0 };
    int    count { 1 };

    void set (int initNum, int initDen, const MeterChange* ch, int n) noexcept
    {
        segStart[0] = 0.0; segBpb[0] = barBeats (initNum, initDen); count = 1;
        for (int i = 0; i < n && count < kMaxSegs; ++i)
        {
            if (ch[i].beat <= 1.0e-9) { segBpb[0] = barBeats (ch[i].num, ch[i].den); continue; }
            segStart[count] = ch[i].beat;
            segBpb  [count] = barBeats (ch[i].num, ch[i].den);
            ++count;
        }
    }

    // Is this beat position the start of a bar (a downbeat) in the effective meter?
    bool isDownbeat (double beat) const noexcept
    {
        int s = 0;
        for (int i = 0; i < count; ++i) { if (segStart[i] <= beat + 1.0e-9) s = i; else break; }
        const double frac = (beat - segStart[s]) / segBpb[s];
        return (frac - std::floor (frac + 1.0e-9)) < 1.0e-6;
    }
};

}   // namespace gloopy::time
