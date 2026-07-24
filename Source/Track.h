// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <atomic>
#include "Generator.h"
#include "Clip.h"

enum class TrackType { Instrument, Audio, MidiOut };

/** A track in the linear arrangement: a sound source (for instrument tracks)
    plus a row of clips on the timeline, and its mix settings. Unifies the old
    Channel + Pattern + playlist-lane into one thing. */
struct Track
{
    Track (juce::String nameToUse, std::unique_ptr<Generator> gen,
           int pitch, juce::Colour col, TrackType t = TrackType::Instrument)
        : name (std::move (nameToUse)), type (t),
          generator (std::move (gen)), defaultPitch (pitch), colour (col)
    {}

    juce::String               name;
    TrackType                  type;
    std::unique_ptr<Generator> generator;     // instrument tracks
    int                        defaultPitch;   // pitch used by step-style editing
    juce::Colour               colour;

    int id { -1 };                             // stable id for the control API
    juce::MidiMessageCollector liveMidi;       // real-time notes injected via OSC

    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan    { 0.0f };
    std::atomic<bool>  mute   { false };
    std::atomic<bool>  solo   { false };
    std::atomic<int>   mixerTrack { 0 };

    // Audio-recording state (project state, not global prefs).
    std::atomic<bool> recordArmed   { false };
    std::atomic<int>  recordInput   { 0 };   // first hardware input channel
    std::atomic<int>  recordChannels{ 2 };   // 1 = mono, 2 = stereo

    std::vector<Clip> clips;   // guarded by the engine lock
};
