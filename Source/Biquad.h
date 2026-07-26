// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cmath>

// RBJ "audio EQ cookbook" biquad coefficients (normalised so a0 == 1), factored out of
// EqFx so the three-band EQ's peak / low-shelf / high-shelf math is unit-testable without
// the audio engine. biquadMagnitudeDb evaluates |H(e^jw)| so a test can assert the shelf
// hits its target gain in-band and is flat out-of-band.

struct BiquadCoeffs { double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };

inline BiquadCoeffs eqPeak (double f, double dB, double Q, double sr)
{
    const double A  = std::pow (10.0, dB / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * (Q > 1e-9 ? Q : 1e-9));
    const double a0 = 1.0 + alpha / A;
    return { (1.0 + alpha * A) / a0, (-2.0 * cw) / a0, (1.0 - alpha * A) / a0,
             (-2.0 * cw) / a0, (1.0 - alpha / A) / a0 };
}

inline BiquadCoeffs eqLowShelf (double f, double dB, double sr)
{
    const double A  = std::pow (10.0, dB / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / 2.0 * std::sqrt (2.0);                 // shelf slope S = 1
    const double beta  = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * cw + beta;
    return { A * ((A + 1.0) - (A - 1.0) * cw + beta) / a0,
             2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0,
             A * ((A + 1.0) - (A - 1.0) * cw - beta) / a0,
             -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0,
             ((A + 1.0) + (A - 1.0) * cw - beta) / a0 };
}

inline BiquadCoeffs eqHighShelf (double f, double dB, double sr)
{
    const double A  = std::pow (10.0, dB / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / 2.0 * std::sqrt (2.0);
    const double beta  = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cw + beta;
    return { A * ((A + 1.0) + (A - 1.0) * cw + beta) / a0,
             -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0,
             A * ((A + 1.0) + (A - 1.0) * cw - beta) / a0,
             2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0,
             ((A + 1.0) - (A - 1.0) * cw - beta) / a0 };
}

// |H(e^jw)| in dB at frequency f (Hz), for tests.
inline double biquadMagnitudeDb (const BiquadCoeffs& c, double f, double sr)
{
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw1 = std::cos (w),  sw1 = std::sin (w);
    const double cw2 = std::cos (2 * w), sw2 = std::sin (2 * w);
    const double nRe = c.b0 + c.b1 * cw1 + c.b2 * cw2;
    const double nIm = -(c.b1 * sw1 + c.b2 * sw2);
    const double dRe = 1.0 + c.a1 * cw1 + c.a2 * cw2;
    const double dIm = -(c.a1 * sw1 + c.a2 * sw2);
    const double mag = std::sqrt ((nRe * nRe + nIm * nIm) / (dRe * dRe + dIm * dIm));
    return 20.0 * std::log10 (mag);
}
