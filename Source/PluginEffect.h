// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include "Effect.h"

/** A hosted effect plugin (VST3/LV2) in a mixer insert chain. */
class PluginEffect : public Effect
{
public:
    explicit PluginEffect (std::unique_ptr<juce::AudioPluginInstance> p)
        : plugin (std::move (p)) {}

    void prepare (double sampleRate, int blockSize, int /*numChannels*/) override
    {
        if (plugin == nullptr) return;
        plugin->setPlayConfigDetails (2, 2, sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
    }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load() || plugin == nullptr) return;
        juce::MidiBuffer midi;
        plugin->processBlock (b, midi);
    }

    juce::String name() const override { return plugin ? plugin->getName() : juce::String ("Plugin"); }
    std::vector<EffectParam> parameters() override { return {}; }   // edited via the plugin editor
    juce::AudioProcessor* getPluginInstance() override { return plugin.get(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
};
