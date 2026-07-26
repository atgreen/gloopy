// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <memory>
#include "Generator.h"

/** Instrument backed by the embedded Surge XT synth engine (surge-common, GPL-3.0;
    see docs/surge-embed.md). PIMPL: the heavy C++20 Surge headers live only in
    SurgeGenerator.cpp (compiled as a C++20 TU), so the rest of Gloopy — which is
    C++17 — sees only this thin Generator interface.

    Surge renders a fixed 32-sample block per process() call; render() adapts that
    to Gloopy's arbitrary block size with an internal carry buffer (no dropped
    samples; up to one Surge-block of latency, which is sub-millisecond).

    Only functional when built with GLOOPY_WITH_SURGE; otherwise the methods are
    inert stubs so a lean build still compiles. */
class SurgeGenerator : public Generator
{
public:
    SurgeGenerator();
    ~SurgeGenerator() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override;
    void allNotesOff() override;
    juce::String typeName() const override { return "Surge"; }

    /** Load a Surge patch (.fxp). Call before installing the generator. */
    bool loadPatch (const juce::File& fxp, juce::String& error);

    /** The Surge factory data directory: $GLOOPY_SURGE_DATA, else the bundled
        curated set (slice 4), else empty. Surge needs this to init + load patches. */
    static juce::File dataDir();

    const juce::String& getName() const { return displayName; }
    int numPatches() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::String displayName { "Surge" };
};
