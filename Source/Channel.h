// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include "Generator.h"

/** One row in the Channel Rack: a named sound source plus its mix settings.
    Step/note data lives in the Pattern, not here (FL's separation of "what a
    sound is" from "when it plays"). */
struct Channel
{
    Channel (juce::String nameToUse, std::unique_ptr<Generator> gen,
             int pitch, juce::Colour col)
        : name (std::move (nameToUse)),
          generator (std::move (gen)),
          defaultPitch (pitch),
          colour (col)
    {}

    juce::String               name;
    std::unique_ptr<Generator> generator;
    int                        defaultPitch;   // note a toggled step plays
    juce::Colour               colour;

    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan    { 0.0f };
    std::atomic<bool>  mute   { false };
    std::atomic<bool>  solo   { false };
    std::atomic<int>   mixerTrack { 0 };   // index into the mixer (0 = master)
};
