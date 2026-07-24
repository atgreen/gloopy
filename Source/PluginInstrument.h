// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

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
            // Hard stop: All Sound Off (CC120) kills voices immediately — All Notes
            // Off (CC123) alone only starts the release, which for long sustained/
            // looped samples (e.g. orchestral strings) rings on for seconds.
            juce::MidiBuffer m;
            for (int ch = 1; ch <= 16; ++ch)
            {
                m.addEvent (juce::MidiMessage::controllerEvent (ch, 120, 0), 0);  // All Sound Off
                m.addEvent (juce::MidiMessage::controllerEvent (ch, 123, 0), 0);  // All Notes Off
                m.addEvent (juce::MidiMessage::controllerEvent (ch,  64, 0), 0);  // Sustain off
            }
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
