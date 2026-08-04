// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "Generator.h"
#include "Sampler.h"

/** A multi-pad drum kit as ONE generator (one track, one clip). Each pad is a
    named one-shot voice bound to a MIDI note; internally a pad is just a Sampler
    playing its own buffer at its natural pitch, so all of the Sampler's playback
    (window, fades, choke, interpolation) is reused per pad.

    render() routes each incoming note to the pad whose note matches and sums the
    pads into the shared buffer. voices() exposes the pads in row order, which is
    what the step editor draws as lanes — the kit shapes the grid. */
class DrumKit : public Generator
{
public:
    struct Pad
    {
        juce::String             name;
        juce::Colour             colour { 0xff9aa0a6 };
        int                      note   { 36 };
        std::unique_ptr<Sampler> sampler;
    };

    DrumKit() = default;

    void prepare (double sampleRate, int maxBlockSize) override
    {
        deviceRate = sampleRate;
        blockSize  = maxBlockSize;
        for (auto& p : pads)
            if (p.sampler) p.sampler->prepare (sampleRate, maxBlockSize);
    }

    /** Add a pad from raw sample data. The pad plays at its natural pitch: its
        Sampler root note is set to @p note so a hit on that note isn't shifted. */
    Sampler& addPad (juce::String name, juce::Colour colour, int note,
                     juce::AudioBuffer<float> buffer, double sourceSampleRate)
    {
        auto s = std::make_unique<Sampler>();
        s->prepare (deviceRate, blockSize);
        s->setSample (std::move (buffer), sourceSampleRate, name);
        s->setRootNote (note);
        Sampler& ref = *s;
        pads.push_back ({ std::move (name), colour, note, std::move (s) });
        return ref;
    }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override
    {
        for (auto& p : pads)
        {
            if (p.sampler == nullptr)
                continue;

            // Feed each pad only the events on its note. Reuse one scratch buffer to
            // avoid per-block allocation on the audio thread.
            scratch.clear();
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if ((m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() == p.note)
                    scratch.addEvent (m, meta.samplePosition);
            }
            p.sampler->render (buffer, scratch, startSample, numSamples);   // adds into buffer
        }
    }

    void allNotesOff() override
    {
        for (auto& p : pads)
            if (p.sampler) p.sampler->allNotesOff();
    }

    void setFreewheeling (bool offline) override
    {
        for (auto& p : pads)
            if (p.sampler) p.sampler->setFreewheeling (offline);
    }

    juce::String typeName() const override { return "DrumKit"; }

    std::vector<Voice> voices() const override
    {
        std::vector<Voice> v;
        v.reserve (pads.size());
        for (const auto& p : pads)
            v.push_back ({ p.name, p.note, p.colour });
        return v;
    }

    // Accessors for project serialization.
    const std::vector<Pad>& getPads() const noexcept { return pads; }
    int  numPads() const noexcept { return (int) pads.size(); }

private:
    std::vector<Pad> pads;
    juce::MidiBuffer scratch;                 // per-pad note routing (reused each block)
    double deviceRate { 44100.0 };
    int    blockSize  { 512 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKit)
};
