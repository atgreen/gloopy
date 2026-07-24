// SPDX-FileCopyrightText: 2026 Anthony Green <anthony@atgreen.org>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <cmath>

/** Procedurally synthesizes a tiny drum kit into mono AudioBuffers so the app
    ships with usable sounds and no external files. Generated at a fixed 44.1 kHz;
    the Sampler corrects for the device rate on playback. */
namespace DrumSynth
{
    constexpr double kRate = 44100.0;
    constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

    inline juce::AudioBuffer<float> makeKick()
    {
        const int len = (int) (0.45 * kRate);
        juce::AudioBuffer<float> b (1, len);
        auto* d = b.getWritePointer (0);
        juce::Random rng (0x1);
        double phase = 0.0;

        for (int i = 0; i < len; ++i)
        {
            const double t    = i / kRate;
            const double freq = 50.0 + 100.0 * std::exp (-t / 0.03);   // pitch drop 150→50 Hz
            phase += freq / kRate;
            const double env  = std::exp (-t / 0.14);
            double s = std::sin (phase * kTwoPi) * env;

            if (t < 0.004)                                             // beater click
                s += (rng.nextFloat() * 2.0f - 1.0f) * 0.4 * std::exp (-t / 0.001);

            d[i] = (float) (s * 0.9);
        }
        return b;
    }

    inline juce::AudioBuffer<float> makeSnare()
    {
        const int len = (int) (0.20 * kRate);
        juce::AudioBuffer<float> b (1, len);
        auto* d = b.getWritePointer (0);
        juce::Random rng (0x2);

        for (int i = 0; i < len; ++i)
        {
            const double t     = i / kRate;
            const double env   = std::exp (-t / 0.08);
            const double noise = (rng.nextFloat() * 2.0f - 1.0f) * env;
            const double tone  = std::sin (kTwoPi * 180.0 * t) * std::exp (-t / 0.05);
            d[i] = (float) ((noise * 0.7 + tone * 0.5) * 0.8);
        }
        return b;
    }

    inline juce::AudioBuffer<float> makeHat()
    {
        const int len = (int) (0.08 * kRate);
        juce::AudioBuffer<float> b (1, len);
        auto* d = b.getWritePointer (0);
        juce::Random rng (0x3);
        float prev = 0.0f;

        for (int i = 0; i < len; ++i)
        {
            const double t   = i / kRate;
            const double env = std::exp (-t / 0.015);
            const float  n   = rng.nextFloat() * 2.0f - 1.0f;
            const float  hp  = n - prev;   // crude high-pass (differentiator)
            prev = n;
            d[i] = (float) (hp * env * 0.5);
        }
        return b;
    }

    inline juce::AudioBuffer<float> makeClap()
    {
        const int len = (int) (0.18 * kRate);
        juce::AudioBuffer<float> b (1, len);
        auto* d = b.getWritePointer (0);
        juce::Random rng (0x4);
        const double bursts[] = { 0.0, 0.008, 0.016, 0.024 };

        for (int i = 0; i < len; ++i)
        {
            const double t = i / kRate;
            double amp = 0.0;
            for (double k : bursts)
                if (t >= k)
                    amp += std::exp (-(t - k) / 0.006);

            const double tail  = std::exp (-t / 0.08);
            const float  noise = rng.nextFloat() * 2.0f - 1.0f;
            d[i] = (float) (noise * (juce::jmin (amp, 1.0) * 0.6 + tail * 0.2) * 0.9);
        }
        return b;
    }
}
