// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include "Generator.h"
#include "sfizz.hpp"

/** SFZ instrument backed by the vendored sfizz library (third_party/sfizz),
    linked directly — no plugin hosting, no state-injection hack. sfizz is a
    complete SFZ implementation with disk streaming, so it handles everything
    from a single VPO section to the full multi-GB Salamander piano within a
    small memory footprint. We just feed it MIDI and pull audio.

    The project stores only the .sfz path; sfizz re-parses on load. */
class SfizzGenerator : public Generator
{
public:
    void prepare (double sampleRate, int maxBlockSize) override
    {
        maxBlock = juce::jmax (1, maxBlockSize > 0 ? maxBlockSize : 512);
        synth.setSampleRate ((float) sampleRate);
        synth.setSamplesPerBlock (maxBlock);
        synth.setNumVoices (64);
        scratch.setSize (2, maxBlock, false, false, true);
    }

    /** Load an .sfz. Call on the message thread before installing the generator. */
    bool loadSfz (const juce::File& f, juce::String& error)
    {
        if (! f.existsAsFile())
        {
            error = "SFZ file not found: " + f.getFullPathName();
            return false;
        }
        if (! synth.loadSfzFile (f.getFullPathName().toStdString()))
        {
            error = "sfizz failed to parse " + f.getFileName();
            return false;
        }
        sfzPath     = f.getFullPathName();
        displayName = f.getFileNameWithoutExtension();
        std::cout << "[sfz] " << displayName << ": " << synth.getNumRegions()
                  << " regions (sfizz)" << std::endl;
        return true;
    }

    const juce::String& getSfzPath() const { return sfzPath; }
    const juce::String& getName()    const { return displayName; }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override
    {
        if (sfzPath.isEmpty() || maxBlock <= 0)
            return;

        auto it = midi.cbegin();
        const auto end = midi.cend();
        const int outCh = buffer.getNumChannels();

        // sfizz renders a block at a time (≤ the size it was prepared for) and
        // takes MIDI events as sample-delays within that block, so we walk the
        // callback in ≤ maxBlock chunks, dispatch each chunk's events, render
        // into the scratch stereo buffer, and mix into the destination.
        int done = 0;
        while (done < numSamples)
        {
            const int chunk = std::min (maxBlock, numSamples - done);
            const int chunkStart = startSample + done;

            while (it != end && (*it).samplePosition < chunkStart + chunk)
            {
                const auto m = (*it).getMessage();
                const int delay = juce::jlimit (0, chunk - 1, (*it).samplePosition - chunkStart);
                if      (m.isNoteOn())     synth.noteOn  (delay, m.getNoteNumber(), m.getVelocity());
                else if (m.isNoteOff())    synth.noteOff (delay, m.getNoteNumber(), 0);
                else if (m.isController()) synth.cc      (delay, m.getControllerNumber(), m.getControllerValue());
                else if (m.isPitchWheel()) synth.pitchWheel (delay, m.getPitchWheelValue() - 8192);
                ++it;
            }

            float* bufs[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
            synth.renderBlock (bufs, (size_t) chunk, 1);   // writes (overwrites) scratch

            if (outCh >= 2)
            {
                buffer.addFrom (0, chunkStart, scratch, 0, 0, chunk);
                buffer.addFrom (1, chunkStart, scratch, 1, 0, chunk);
            }
            else
            {
                buffer.addFrom (0, chunkStart, scratch, 0, 0, chunk, 0.5f);
                buffer.addFrom (0, chunkStart, scratch, 1, 0, chunk, 0.5f);
            }

            done += chunk;
        }
    }

    void allNotesOff() override { synth.allSoundOff(); }

    void setFreewheeling (bool offline) override
    {
        if (offline) synth.enableFreeWheeling();
        else         synth.disableFreeWheeling();
    }

    juce::String typeName() const override { return "Sfz"; }

private:
    sfz::Sfizz               synth;
    juce::AudioBuffer<float> scratch;
    int                      maxBlock { 0 };
    juce::String             sfzPath, displayName;
};
