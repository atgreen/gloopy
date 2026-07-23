#pragma once

#include <JuceHeader.h>
#include "SynthVoice.h"

/** Thin wrapper around juce::Synthesiser that owns the voices, the sound and
    the shared parameter block. */
class SynthEngine
{
public:
    SynthEngine()
    {
        for (int i = 0; i < kNumVoices; ++i)
            synth.addVoice (new SynthVoice (params));

        synth.addSound (new SynthSound());
        synth.setNoteStealingEnabled (true);
    }

    void prepare (double sampleRate)
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);
    }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples)
    {
        synth.renderNextBlock (buffer, midi, startSample, numSamples);
    }

    void allNotesOff()
    {
        synth.allNotesOff (1, false);
    }

    SynthParams params;

private:
    static constexpr int kNumVoices = 16;
    juce::Synthesiser synth;
};
