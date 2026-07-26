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

    // Aux sends: an additive tap of this insert's post-effects signal into another
    // mixer track (a bus). `isBus` marks a track that exists to receive sends and
    // sum to master (no track routes its main output to it). Guarded by engineLock.
    struct Send { int bus; float level; bool postFader { false }; };   // postFader: send follows the fader/mute
    std::vector<Send> sends;
    bool isBus { false };

    // VCA-lite: the name of the control group this insert belongs to (empty = none).
    // The group's fader SCALES this insert's volume in the mix; it is not extra audio
    // routing. Membership travels with the insert, so it survives insert re-indexing.
    // Guarded by the engine lock.
    juce::String group;
};
