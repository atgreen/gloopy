#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "Generator.h"

/** A hosted instrument plugin (VST3/LV2) as a track generator: MIDI in → audio out. */
class PluginInstrument : public Generator
{
public:
    explicit PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> p)
        : plugin (std::move (p)) {}

    void prepare (double sampleRate, int blockSize) override
    {
        if (plugin == nullptr) return;
        plugin->setPlayConfigDetails (0, 2, sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
    }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override
    {
        if (plugin == nullptr) return;
        juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(),
                                      buffer.getNumChannels(), startSample, numSamples);
        if (panic.exchange (false))
        {
            juce::MidiBuffer m;
            for (int ch = 1; ch <= 16; ++ch)
                m.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
            m.addEvents (midi, 0, numSamples, 0);
            plugin->processBlock (sub, m);
        }
        else
        {
            plugin->processBlock (sub, midi);
        }
    }

    void allNotesOff() override { panic = true; }
    juce::String typeName() const override { return "Plugin"; }
    juce::AudioProcessor* getPluginInstance() override { return plugin.get(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::atomic<bool> panic { false };
};
