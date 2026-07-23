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

    virtual juce::String name() const = 0;
    virtual std::vector<EffectParam> parameters() = 0;

    std::atomic<bool> bypassed { false };
};
