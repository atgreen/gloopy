// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <cmath>

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

    Three domains: beats, seconds, and samples (samples are discrete, so integer-backed). The
    conversion helpers at the bottom express the constant-tempo/constant-rate beats↔seconds↔samples
    layer in these types (the empty-tempo-map fast path); tempo-map-aware conversion stays in the
    engine's TempoConv. Adoption is incremental: this is the foundation. Migrating clip/loop/marker
    positions and the engine's beatToSamples/samplesToBeats call sites onto these types comes in
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

// ---- samples (discrete; integer-backed) ------------------------------------------------------

struct SampleDuration
{
    std::int64_t samples {};
    constexpr SampleDuration() = default;
    constexpr explicit SampleDuration (std::int64_t s) : samples (s) {}
    constexpr std::int64_t inSamples() const { return samples; }
};

struct SamplePosition
{
    std::int64_t samples {};
    constexpr SamplePosition() = default;
    constexpr explicit SamplePosition (std::int64_t s) : samples (s) {}
    constexpr std::int64_t inSamples() const { return samples; }
};

constexpr SamplePosition operator+ (SamplePosition p, SampleDuration d) { return SamplePosition { p.samples + d.samples }; }
constexpr SamplePosition operator+ (SampleDuration d, SamplePosition p) { return SamplePosition { p.samples + d.samples }; }
constexpr SamplePosition operator- (SamplePosition p, SampleDuration d) { return SamplePosition { p.samples - d.samples }; }
constexpr SampleDuration operator- (SamplePosition a, SamplePosition b) { return SampleDuration { a.samples - b.samples }; }
constexpr SampleDuration operator+ (SampleDuration a, SampleDuration b) { return SampleDuration { a.samples + b.samples }; }
constexpr SampleDuration operator- (SampleDuration a, SampleDuration b) { return SampleDuration { a.samples - b.samples }; }
constexpr SampleDuration operator- (SampleDuration a)                   { return SampleDuration { -a.samples }; }
constexpr SampleDuration operator* (SampleDuration d, std::int64_t s)   { return SampleDuration { d.samples * s }; }
constexpr SampleDuration operator* (std::int64_t s, SampleDuration d)   { return SampleDuration { d.samples * s }; }
constexpr SampleDuration operator/ (SampleDuration d, std::int64_t s)   { return SampleDuration { d.samples / s }; }
constexpr double         operator/ (SampleDuration a, SampleDuration b) { return (double) a.samples / (double) b.samples; }  // ratio

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
GLOOPY_TIME_COMPARISONS (SamplePosition, samples)
GLOOPY_TIME_COMPARISONS (SampleDuration, samples)

#undef GLOOPY_TIME_COMPARISONS

// ---- conversions between domains -------------------------------------------------------------
// Constant-tempo / constant-rate conversions (the empty-tempo-map fast path). Tempo-map-aware
// conversion stays in the engine's TempoConv; these are the typed foundation for it. The domain
// (beats vs seconds vs samples) is chosen by the argument type — mixing domains simply won't
// compile, so a conversion can't be called with the wrong kind of time.

constexpr double secondsPerBeat (double bpm) { return 60.0 / bpm; }

constexpr TimePosition toSeconds (BeatPosition p, double bpm) { return TimePosition { p.inBeats() * secondsPerBeat (bpm) }; }
constexpr TimeDuration toSeconds (BeatDuration d, double bpm) { return TimeDuration { d.inBeats() * secondsPerBeat (bpm) }; }
constexpr BeatPosition toBeats   (TimePosition p, double bpm) { return BeatPosition { p.inSeconds() / secondsPerBeat (bpm) }; }
constexpr BeatDuration toBeats   (TimeDuration d, double bpm) { return BeatDuration { d.inSeconds() / secondsPerBeat (bpm) }; }

// seconds <-> samples (round positions/durations to the nearest whole sample).
inline SamplePosition toSamples (TimePosition p, double sampleRate) { return SamplePosition { (std::int64_t) std::llround (p.inSeconds() * sampleRate) }; }
inline SampleDuration toSamples (TimeDuration d, double sampleRate) { return SampleDuration { (std::int64_t) std::llround (d.inSeconds() * sampleRate) }; }
constexpr TimePosition toSeconds (SamplePosition p, double sampleRate) { return TimePosition { (double) p.inSamples() / sampleRate }; }
constexpr TimeDuration toSeconds (SampleDuration d, double sampleRate) { return TimeDuration { (double) d.inSamples() / sampleRate }; }

// beats <-> samples (compose through seconds) — the typed beatToSamples/samplesToBeats.
inline SamplePosition toSamples (BeatPosition p, double bpm, double sampleRate) { return toSamples (toSeconds (p, bpm), sampleRate); }
inline SampleDuration toSamples (BeatDuration d, double bpm, double sampleRate) { return toSamples (toSeconds (d, bpm), sampleRate); }
inline BeatPosition   toBeats   (SamplePosition p, double bpm, double sampleRate) { return toBeats (toSeconds (p, sampleRate), bpm); }
inline BeatDuration   toBeats   (SampleDuration d, double bpm, double sampleRate) { return toBeats (toSeconds (d, sampleRate), bpm); }

} // namespace gloopy::time
