#pragma once

#include <JuceHeader.h>

/** A sound source for one channel. Both the sampler and the synth implement
    this, so the audio engine can drive every channel uniformly with note
    events (a juce::MidiBuffer) and mix the result. */
class Generator
{
public:
    virtual ~Generator() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Render @p numSamples into @p buffer starting at @p startSample, applying
        the note events in @p midi (timestamps relative to @p startSample). */
    virtual void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                         int startSample, int numSamples) = 0;

    virtual void allNotesOff() = 0;

    virtual juce::String typeName() const = 0;

    /** Non-null for hosted plugins, so the UI can open their editor. */
    virtual juce::AudioProcessor* getPluginInstance() { return nullptr; }
};
