// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <numeric>   // std::gcd
#include <cmath>

/** Exact musical time as rationals.

    `Source/TimeTypes.h` gives strong *typed* time over a bare `double` — correct for DSP and the
    scheduler, but as the *stored* representation of musical positions doubles drift: quantize,
    loop boundaries, and equality ("does this note start exactly on the bar?") accumulate
    floating-point error. Musical positions land on a grid (1/4, 1/16, triplets…), so store them
    as exact fractions instead. Convert to `double` only at the engine boundary (TempoConv /
    beatToSamples), where DSP needs it. Modelled on radium's ratio_type.h. Header-only, no deps.

    Two types, on purpose:
      - `BeatRatio`   — arithmetic time, always gcd-reduced (3/4 and 6/8 ARE the same position).
      - `StaticRatio` — notational time (time signatures, grid divisions), deliberately NOT reduced,
                        because 3/4 and 6/8 are the same number but different *meaning*; reducing
                        would destroy it. (radium's cautionary tale.)

    Signed int64 throughout (radium logged a scar: an unsigned time counter was "extremely
    dangerous"). Overflow note: arithmetic/compare cross-multiply, so num·den must stay within
    int64. For realistic musical values (den ≤ ~1920, positions ≤ a long song in beats) the
    products are ~1e12 — comfortably inside int64's ~9.2e18. */

namespace gloopy::time
{

// ---- arithmetic time: reduced rational beats -------------------------------------------------

struct BeatRatio
{
    std::int64_t num { 0 };
    std::int64_t den { 1 };          // always > 0, and the fraction is always gcd-reduced

    constexpr BeatRatio() = default;
    constexpr BeatRatio (std::int64_t n, std::int64_t d) : num (n), den (d) { reduce(); }
    static constexpr BeatRatio whole (std::int64_t beats) { return BeatRatio { beats, 1 }; }

    constexpr void reduce()
    {
        if (den == 0) { num = 0; den = 1; return; }            // degenerate → 0 (never divide by 0)
        if (den < 0)  { num = -num; den = -den; }              // keep the sign in the numerator
        const std::int64_t g = std::gcd (num < 0 ? -num : num, den);
        if (g > 1) { num /= g; den /= g; }
    }

    constexpr double toBeats() const { return (double) num / (double) den; }
    constexpr bool   isZero()  const { return num == 0; }

    /** Nearest rational to a double on a fixed grid — for migrating stored `double` beats.
        maxDen defaults to 960 PPQN (divisible by 2/3/4/5/6/8/… so it lands common musical grids
        exactly). Reduces afterwards, so a value already on a coarser grid comes back coarse. */
    static BeatRatio fromBeats (double beats, std::int64_t maxDen = 960)
    {
        return BeatRatio { (std::int64_t) std::llround (beats * (double) maxDen), maxDen };
    }
};

inline BeatRatio operator+ (BeatRatio a, BeatRatio b) { return { a.num * b.den + b.num * a.den, a.den * b.den }; }
inline BeatRatio operator- (BeatRatio a, BeatRatio b) { return { a.num * b.den - b.num * a.den, a.den * b.den }; }
inline BeatRatio operator- (BeatRatio a)              { return { -a.num, a.den }; }
inline BeatRatio operator* (BeatRatio a, std::int64_t s) { return { a.num * s, a.den }; }
inline BeatRatio operator* (std::int64_t s, BeatRatio a) { return { a.num * s, a.den }; }

// Both operands are reduced with den > 0, so == is exact and < is a safe cross-multiply.
inline bool operator== (BeatRatio a, BeatRatio b) { return a.num == b.num && a.den == b.den; }
inline bool operator!= (BeatRatio a, BeatRatio b) { return ! (a == b); }
inline bool operator<  (BeatRatio a, BeatRatio b) { return a.num * b.den <  b.num * a.den; }
inline bool operator<= (BeatRatio a, BeatRatio b) { return a.num * b.den <= b.num * a.den; }
inline bool operator>  (BeatRatio a, BeatRatio b) { return b <  a; }
inline bool operator>= (BeatRatio a, BeatRatio b) { return b <= a; }

// ---- notational time: non-reduced rational ---------------------------------------------------

/** Time signatures and grid divisions, where the literal num/den carry meaning: 3/4 ≠ 6/8 even
    though the numbers are equal. Never reduced. Denominator may be 0 (radium's "unset"), so
    guard before dividing. Compare with sameNotation, not numeric equality. */
struct StaticRatio
{
    std::int64_t num { 0 };
    std::int64_t den { 0 };

    constexpr StaticRatio() = default;
    constexpr StaticRatio (std::int64_t n, std::int64_t d) : num (n), den (d) {}   // NOT reduced, by design

    constexpr double toDouble() const { return den != 0 ? (double) num / (double) den : 0.0; }
    constexpr bool   sameNotation (StaticRatio o) const { return num == o.num && den == o.den; }
};

}   // namespace gloopy::time
