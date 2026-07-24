// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

/** Live synth parameters, shared between the GUI (writer) and every voice
    (readers). Atomics keep it lock-free across the thread boundary. */
struct SynthParams
{
    std::atomic<int>   waveform { 1 };     // 0 sine, 1 saw, 2 square, 3 triangle
    std::atomic<float> attack   { 0.01f };
    std::atomic<float> decay    { 0.15f };
    std::atomic<float> sustain  { 0.70f };
    std::atomic<float> release  { 0.25f };
    std::atomic<float> gain     { 0.25f };
};

/** Marker sound: our single voice type plays every note on every channel. */
struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote    (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

/** A polyphonic oscillator voice with a selectable waveform and an ADSR
    amplitude envelope. Naive (non-band-limited) oscillators — fine for an MVP;
    band-limiting can come later. */
class SynthVoice : public juce::SynthesiserVoice
{
public:
    explicit SynthVoice (SynthParams& p) : params (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        phase      = 0.0;
        level      = velocity;
        phaseDelta = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber) / getSampleRate();

        adsr.setSampleRate (getSampleRate());
        updateAdsrParameters();
        adsr.noteOn();
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! adsr.isActive())
            return;

        updateAdsrParameters();
        const int   wf = params.waveform.load();
        const float g  = params.gain.load();

        for (int i = 0; i < numSamples; ++i)
        {
            const float env    = adsr.getNextSample();
            const float sample = oscillator (phase, wf) * env * level * g;

            for (int ch = outputBuffer.getNumChannels(); --ch >= 0;)
                outputBuffer.addSample (ch, startSample + i, sample);

            phase += phaseDelta;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        if (! adsr.isActive())
            clearCurrentNote();
    }

private:
    void updateAdsrParameters()
    {
        juce::ADSR::Parameters p;
        p.attack  = params.attack.load();
        p.decay   = params.decay.load();
        p.sustain = params.sustain.load();
        p.release = params.release.load();
        adsr.setParameters (p);
    }

    /** @param phase in [0, 1)  @param wf waveform index */
    static float oscillator (double phase, int wf)
    {
        switch (wf)
        {
            case 0:  return (float) std::sin (phase * juce::MathConstants<double>::twoPi);
            case 1:  return (float) (2.0 * phase - 1.0);                    // saw
            case 2:  return phase < 0.5 ? 1.0f : -1.0f;                     // square
            case 3:  return (float) (4.0 * std::abs (phase - 0.5) - 1.0);   // triangle
            default: return 0.0f;
        }
    }

    SynthParams& params;
    juce::ADSR   adsr;
    double       phase      { 0.0 };
    double       phaseDelta { 0.0 };
    float        level      { 1.0f };
};
