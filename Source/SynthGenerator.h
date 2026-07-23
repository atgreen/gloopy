#pragma once

#include <JuceHeader.h>
#include "Generator.h"
#include "SynthEngine.h"

/** Wraps the existing polyphonic SynthEngine as a channel generator. */
class SynthGenerator : public Generator
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/) override
    {
        engine.prepare (sampleRate);
    }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override
    {
        engine.render (buffer, midi, startSample, numSamples);
    }

    void allNotesOff() override { engine.allNotesOff(); }

    juce::String typeName() const override { return "Synth"; }

    SynthEngine engine;   // exposes .params for the UI
};
