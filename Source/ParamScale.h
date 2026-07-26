// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <cmath>

// Scaling-aware conversion between a ParamModel value and a 0..1 knob/fader position,
// using the param's declared scaling. UI knobs and external controllers use these so a
// log control (e.g. a filter cutoff 20..20000 Hz) travels perceptually — half-way is the
// geometric mean, not the arithmetic mean. Pure + unit-tested (GloopyTests::ParamScale).
//
// "log" needs a positive [min,max]; "dB" maps the position linearly in decibels over the
// value's amplitude range (min treated as the -inf floor). Everything else is linear.

inline float paramNormalize (float value, float mn, float mx, const juce::String& scaling)
{
    if (mx <= mn) return 0.0f;
    if (scaling == "log" && mn > 0.0f && mx > 0.0f && value > 0.0f)
        return juce::jlimit (0.0f, 1.0f, (float) (std::log (value / mn) / std::log (mx / mn)));
    if (scaling == "dB" && mx > 0.0f && value > 0.0f)
    {
        const double lo = mn > 0.0f ? 20.0 * std::log10 (mn) : -60.0;   // floor when min is 0
        const double hi = 20.0 * std::log10 (mx);
        return hi > lo ? juce::jlimit (0.0f, 1.0f, (float) ((20.0 * std::log10 (value) - lo) / (hi - lo))) : 0.0f;
    }
    return juce::jlimit (0.0f, 1.0f, (value - mn) / (mx - mn));         // linear / enum / bool
}

inline float paramDenormalize (float pos, float mn, float mx, const juce::String& scaling)
{
    pos = juce::jlimit (0.0f, 1.0f, pos);
    if (scaling == "log" && mn > 0.0f && mx > 0.0f)
        return (float) (mn * std::pow ((double) mx / mn, (double) pos));
    if (scaling == "dB" && mx > 0.0f)
    {
        const double lo = mn > 0.0f ? 20.0 * std::log10 (mn) : -60.0;
        const double hi = 20.0 * std::log10 (mx);
        return (float) std::pow (10.0, (lo + (double) pos * (hi - lo)) / 20.0);
    }
    return mn + pos * (mx - mn);                                        // linear
}
