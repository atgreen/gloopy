// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

/** Strong time types — a *position* on a timeline and a *duration* (length) are distinct types,
    and beats and seconds are distinct domains, so the compiler rejects nonsense like adding two
    positions or mixing beats with seconds. Only the sensible combinations exist:

        position - position = duration        (how far apart)
        position + duration = position        (move forward)
        position - duration = position        (move back)
        duration ± duration = duration
        scalar   * duration = duration        (and duration * scalar, duration / scalar)
        duration / duration = double          (a ratio)

    Each wraps a bare `double`; you can only get in/out through the explicit constructor and the
    named accessor (`inBeats()` / `inSeconds()`), so a raw number can never silently stand in for
    a typed time. Header-only, constexpr. Modelled on tracktion_core's tracktion_Time.h.

    Adoption is incremental: this is the foundation. Migrating clip/loop/marker positions and the
    beatToSamples/samplesToBeats conversion layer onto these types (plus a Sample domain) comes in
    later slices — the Gloopy roadmap's Wave 7 #21. */

namespace gloopy::time
{

// ---- beats -----------------------------------------------------------------------------------

struct BeatDuration
{
    double beats {};
    constexpr BeatDuration() = default;
    constexpr explicit BeatDuration (double b) : beats (b) {}
    constexpr double inBeats() const { return beats; }
};

struct BeatPosition
{
    double beats {};
    constexpr BeatPosition() = default;
    constexpr explicit BeatPosition (double b) : beats (b) {}
    constexpr double inBeats() const { return beats; }
};

constexpr BeatPosition operator+ (BeatPosition p, BeatDuration d) { return BeatPosition { p.beats + d.beats }; }
constexpr BeatPosition operator+ (BeatDuration d, BeatPosition p) { return BeatPosition { p.beats + d.beats }; }
constexpr BeatPosition operator- (BeatPosition p, BeatDuration d) { return BeatPosition { p.beats - d.beats }; }
constexpr BeatDuration operator- (BeatPosition a, BeatPosition b) { return BeatDuration { a.beats - b.beats }; }
constexpr BeatDuration operator+ (BeatDuration a, BeatDuration b) { return BeatDuration { a.beats + b.beats }; }
constexpr BeatDuration operator- (BeatDuration a, BeatDuration b) { return BeatDuration { a.beats - b.beats }; }
constexpr BeatDuration operator- (BeatDuration a)                 { return BeatDuration { -a.beats }; }
constexpr BeatDuration operator* (BeatDuration d, double s)       { return BeatDuration { d.beats * s }; }
constexpr BeatDuration operator* (double s, BeatDuration d)       { return BeatDuration { d.beats * s }; }
constexpr BeatDuration operator/ (BeatDuration d, double s)       { return BeatDuration { d.beats / s }; }
constexpr double       operator/ (BeatDuration a, BeatDuration b) { return a.beats / b.beats; }   // ratio

// ---- seconds ---------------------------------------------------------------------------------

struct TimeDuration
{
    double seconds {};
    constexpr TimeDuration() = default;
    constexpr explicit TimeDuration (double s) : seconds (s) {}
    constexpr double inSeconds() const { return seconds; }
};

struct TimePosition
{
    double seconds {};
    constexpr TimePosition() = default;
    constexpr explicit TimePosition (double s) : seconds (s) {}
    constexpr double inSeconds() const { return seconds; }
};

constexpr TimePosition operator+ (TimePosition p, TimeDuration d) { return TimePosition { p.seconds + d.seconds }; }
constexpr TimePosition operator+ (TimeDuration d, TimePosition p) { return TimePosition { p.seconds + d.seconds }; }
constexpr TimePosition operator- (TimePosition p, TimeDuration d) { return TimePosition { p.seconds - d.seconds }; }
constexpr TimeDuration operator- (TimePosition a, TimePosition b) { return TimeDuration { a.seconds - b.seconds }; }
constexpr TimeDuration operator+ (TimeDuration a, TimeDuration b) { return TimeDuration { a.seconds + b.seconds }; }
constexpr TimeDuration operator- (TimeDuration a, TimeDuration b) { return TimeDuration { a.seconds - b.seconds }; }
constexpr TimeDuration operator- (TimeDuration a)                 { return TimeDuration { -a.seconds }; }
constexpr TimeDuration operator* (TimeDuration d, double s)       { return TimeDuration { d.seconds * s }; }
constexpr TimeDuration operator* (double s, TimeDuration d)       { return TimeDuration { d.seconds * s }; }
constexpr TimeDuration operator/ (TimeDuration d, double s)       { return TimeDuration { d.seconds / s }; }
constexpr double       operator/ (TimeDuration a, TimeDuration b) { return a.seconds / b.seconds; } // ratio

// ---- comparisons (== / != via <= to keep the exact-double compare out of -Wfloat-equal) -------

#define GLOOPY_TIME_COMPARISONS(T, F) \
    constexpr bool operator<  (T a, T b) { return a.F <  b.F; } \
    constexpr bool operator<= (T a, T b) { return a.F <= b.F; } \
    constexpr bool operator>  (T a, T b) { return a.F >  b.F; } \
    constexpr bool operator>= (T a, T b) { return a.F >= b.F; } \
    constexpr bool operator== (T a, T b) { return a.F <= b.F && b.F <= a.F; } \
    constexpr bool operator!= (T a, T b) { return ! (a == b); }

GLOOPY_TIME_COMPARISONS (BeatPosition, beats)
GLOOPY_TIME_COMPARISONS (BeatDuration, beats)
GLOOPY_TIME_COMPARISONS (TimePosition, seconds)
GLOOPY_TIME_COMPARISONS (TimeDuration, seconds)

#undef GLOOPY_TIME_COMPARISONS

} // namespace gloopy::time
