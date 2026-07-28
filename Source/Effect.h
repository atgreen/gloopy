// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>
#include <atomic>

/** A single automatable parameter, exposed generically so the mixer UI can
    build sliders for any effect without knowing its type. */
struct EffectParam
{
    juce::String              name;
    float                     minValue, maxValue, defaultValue;
    std::function<float()>    get;
    std::function<void(float)> set;
};

/** An in-place stereo audio effect in a mixer track's insert chain. */
class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate, int blockSize, int numChannels) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;
    virtual void reset() {}

    /** Called each block before process() with the current project tempo, so a
        tempo-syncable effect (e.g. Delay) can convert beat divisions to samples. */
    virtual void setTempo (double /*bpm*/) {}

    virtual juce::String name() const = 0;
    virtual std::vector<EffectParam> parameters() = 0;

    /** Non-null for hosted plugins, so the UI can open their editor. */
    virtual juce::AudioProcessor* getPluginInstance() { return nullptr; }

    /** Analyzer snapshot: a non-mutating "meter" effect (e.g. an oscilloscope) fills up to
        maxN floats of its captured signal and returns the count. Returns 0 for a normal
        (non-analyzer) effect. Reads lock-free atomics, so it's safe to call off the audio
        thread while it runs. */
    virtual int analyzerSnapshot (float* /*out*/, int /*maxN*/) const { return 0; }

    std::atomic<bool> bypassed { false };
};
