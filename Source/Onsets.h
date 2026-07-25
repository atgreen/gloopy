// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// Pure onset (transient) detection for audio-clip slicing, factored out so it's
// unit-testable without the engine. An energy-flux novelty function (the positive
// rise in short-frame log-energy) is peak-picked above a mean+sensitivity*std
// threshold, with a minimum inter-onset gap to avoid double-triggers. No FFT — a
// rising-energy detector is enough for slicing drums/loops at note edges. The frame
// at sample 0 is never returned (a clip already starts there); the returned sample
// positions are interior cut points, ascending.
inline std::vector<int> detectOnsets (const float* x, int n, double sampleRate, float sensitivity)
{
    std::vector<int> onsets;
    constexpr int hop = 256, win = 512;
    if (x == nullptr || n < win + hop || sampleRate <= 0.0) return onsets;

    const int frames = (n - win) / hop + 1;
    if (frames < 3) return onsets;

    std::vector<double> nov ((size_t) frames, 0.0);
    double prevLogE = 0.0;
    for (int k = 0; k < frames; ++k)
    {
        double e = 0.0;
        const int s0 = k * hop;
        for (int i = 0; i < win; ++i) { const double v = x[s0 + i]; e += v * v; }
        const double logE = std::log (e + 1.0e-9);
        nov[(size_t) k] = k == 0 ? 0.0 : std::max (0.0, logE - prevLogE);   // rising energy only
        prevLogE = logE;
    }

    // Adaptive threshold: mean + sensitivity * std of the novelty (higher sensitivity
    // => fewer, stronger onsets).
    double mean = 0.0; for (double v : nov) mean += v; mean /= (double) frames;
    double var  = 0.0; for (double v : nov) var += (v - mean) * (v - mean); var /= (double) frames;
    const double thr = mean + (double) sensitivity * std::sqrt (var);

    const int minGap = std::max (1, (int) (0.05 * sampleRate));   // 50 ms between onsets
    int last = -minGap;
    for (int k = 1; k < frames - 1; ++k)
    {
        const double v = nov[(size_t) k];
        if (v > thr && v >= nov[(size_t) (k - 1)] && v >= nov[(size_t) (k + 1)])
        {
            const int pos = k * hop;
            if (pos - last >= minGap) { onsets.push_back (pos); last = pos; }
        }
    }
    return onsets;
}
