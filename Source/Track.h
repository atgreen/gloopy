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

    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan    { 0.0f };
    std::atomic<bool>  mute   { false };
    std::atomic<bool>  solo   { false };
    std::atomic<int>   mixerTrack { 0 };

    std::vector<Clip> clips;   // guarded by the engine lock
};
