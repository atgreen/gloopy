// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <atomic>
#include "Effect.h"

/** A mixer insert (or the master, index 0): a fader, pan, mute/solo, an ordered
    effect chain, a scratch buffer to accumulate its input, and peak meters. */
struct MixerTrack
{
    explicit MixerTrack (juce::String n) : name (std::move (n)) {}

    juce::String name;
    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan    { 0.0f };
    std::atomic<bool>  mute   { false };
    std::atomic<bool>  solo   { false };

    std::atomic<float> peakL { 0.0f };
    std::atomic<float> peakR { 0.0f };
    std::atomic<bool>  clipped { false };   // sticky: set when a block hits >= 0 dBFS

    std::vector<std::unique_ptr<Effect>> effects;   // guarded by the engine lock
    juce::AudioBuffer<float>             buffer;     // audio-thread scratch
};
