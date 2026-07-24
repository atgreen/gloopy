// SPDX-FileCopyrightText: 2026 Anthony Green <anthony@atgreen.org>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "Generator.h"

/** A simple one-shot sample player. Each note-on starts a voice that plays the
    loaded sample from the start; note number sets the playback rate relative to
    a root note. Note-offs are ignored (drum one-shots ring out). */
class Sampler : public Generator
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/) override
    {
        deviceRate = sampleRate;
    }

    /** Provide sample data directly (used by the bundled drum kit). */
    void setSample (juce::AudioBuffer<float> buffer, double sourceSampleRate, juce::String name)
    {
        sample     = std::move (buffer);
        sourceRate = sourceSampleRate;
        sampleName = std::move (name);
        for (auto& v : voices) v.active = false;
    }

    /** Load sample data from an audio file. */
    bool loadFile (const juce::File& file, juce::AudioFormatManager& formatManager)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels,
                                         (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        setSample (std::move (buffer), reader->sampleRate,
                   file.getFileNameWithoutExtension());
        return true;
    }

    void setRootNote (int note) { rootNote = note; }
    int  getRootNote() const { return rootNote; }
    const juce::String& getName() const { return sampleName; }

    // Accessors for project serialization.
    const juce::AudioBuffer<float>& getSampleBuffer() const { return sample; }
    double getSourceRate() const { return sourceRate; }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override
    {
        if (sample.getNumSamples() == 0)
            return;

        auto midiIt = midi.cbegin();
        const auto midiEnd = midi.cend();

        for (int i = 0; i < numSamples; ++i)
        {
            const int framePos = startSample + i;

            // Trigger any voices whose event lands on this frame.
            while (midiIt != midiEnd && (*midiIt).samplePosition <= framePos)
            {
                const auto msg = (*midiIt).getMessage();
                if (msg.isNoteOn())
                    startVoice (msg.getNoteNumber(), msg.getFloatVelocity());
                ++midiIt;
            }

            float mono = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active)
                    continue;

                mono += readInterpolated (v.pos) * v.gain;
                v.pos += v.rate;
                if (v.pos >= (double) (sample.getNumSamples() - 1))
                    v.active = false;
            }

            for (int ch = buffer.getNumChannels(); --ch >= 0;)
                buffer.addSample (ch, framePos, mono);
        }
    }

    void allNotesOff() override
    {
        for (auto& v : voices)
            v.active = false;
    }

    juce::String typeName() const override { return "Sampler"; }

private:
    struct Voice
    {
        bool   active { false };
        double pos    { 0.0 };
        double rate   { 1.0 };
        float  gain   { 1.0f };
    };

    void startVoice (int noteNumber, float velocity)
    {
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active) { slot = &v; break; }
        if (slot == nullptr)
            slot = &voices[0];   // steal the first voice

        const double pitchRatio = std::pow (2.0, (noteNumber - rootNote) / 12.0);
        slot->active = true;
        slot->pos    = 0.0;
        slot->rate   = (sourceRate / deviceRate) * pitchRatio;
        slot->gain   = velocity;
    }

    float readInterpolated (double pos) const
    {
        const int   i0   = (int) pos;
        const int   i1   = i0 + 1;
        const float frac = (float) (pos - i0);
        const auto* d = sample.getReadPointer (0);
        return d[i0] * (1.0f - frac) + d[i1] * frac;
    }

    juce::AudioBuffer<float> sample;
    double      sourceRate { 44100.0 };
    double      deviceRate { 44100.0 };
    int         rootNote   { 60 };
    juce::String sampleName;

    static constexpr int kNumVoices = 8;
    std::array<Voice, kNumVoices> voices;
};
