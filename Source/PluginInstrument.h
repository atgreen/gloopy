// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <bitset>
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
            // Hard stop. sfizz ignores the All Sound/Notes Off controllers, and
            // blasting note-offs makes it emit a release burst, so ask the plugin
            // to clear its own voice state, then release the notes we know were held.
            plugin->reset();
            juce::MidiBuffer m;
            for (int i = 0; i < 16 * 128; ++i)
                if (held[i]) m.addEvent (juce::MidiMessage::noteOff (i / 128 + 1, i % 128), 0);
            held.reset();
            m.addEvents (midi, 0, numSamples, 0);
            trackHeld (m);
            plugin->processBlock (sub, m);
        }
        else
        {
            trackHeld (midi);
            plugin->processBlock (sub, midi);
        }
    }

    void allNotesOff() override { panic = true; }
    juce::String typeName() const override { return "Plugin"; }
    juce::AudioProcessor* getPluginInstance() override { return plugin.get(); }

private:
    /** Track which (channel,note) pairs are currently sounding, so a panic can
        release exactly those. */
    void trackHeld (const juce::MidiBuffer& midi)
    {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())       held.set ((msg.getChannel() - 1) * 128 + msg.getNoteNumber());
            else if (msg.isNoteOff()) held.reset ((msg.getChannel() - 1) * 128 + msg.getNoteNumber());
        }
    }

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::atomic<bool> panic { false };
    std::bitset<16 * 128> held;
};
