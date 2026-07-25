// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

/** Live synth parameters, shared between the GUI (writer) and every voice
    (readers). Atomics keep it lock-free across the thread boundary.

    Defaults are chosen so a patch that touches only the classic controls
    (waveform + amp ADSR + gain) sounds exactly like the original one-oscillator
    engine: osc2 is muted (oscMix 0), no sub, the filter is wide open (20 kHz,
    no envelope mod), and the LFO is silent (depth 0). Projects saved before the
    expansion therefore reload unchanged. */
struct SynthParams
{
    // Oscillator 1
    std::atomic<int>   waveform     { 1 };       // 0 sine, 1 saw, 2 square, 3 triangle

    // Oscillator 2 (detuned layer)
    std::atomic<int>   osc2Wave     { 1 };
    std::atomic<float> osc2Detune   { 0.0f };    // cents relative to osc1
    std::atomic<float> oscMix       { 0.0f };    // 0 = osc1 only … 1 = osc2 only
    std::atomic<float> subLevel     { 0.0f };    // sine one octave below osc1, 0..1

    // Amplitude envelope
    std::atomic<float> attack       { 0.01f };
    std::atomic<float> decay        { 0.15f };
    std::atomic<float> sustain      { 0.70f };
    std::atomic<float> release      { 0.25f };

    // Resonant state-variable filter + its own envelope
    std::atomic<int>   filterType   { 0 };       // 0 low-pass, 1 high-pass, 2 band-pass
    std::atomic<float> cutoff       { 20000.0f };// Hz (wide open = neutral)
    std::atomic<float> resonance    { 0.7f };    // Q (0.5 = none … ~10 = sharp)
    std::atomic<float> filterEnvAmt { 0.0f };    // octaves of cutoff sweep, 0 = neutral
    std::atomic<float> fAttack      { 0.01f };
    std::atomic<float> fDecay       { 0.20f };
    std::atomic<float> fSustain     { 0.60f };
    std::atomic<float> fRelease     { 0.30f };

    // One LFO → pitch / filter / amp
    std::atomic<int>   lfoTarget    { 0 };       // 0 pitch (vibrato), 1 filter, 2 amp (tremolo)
    std::atomic<float> lfoRate      { 5.0f };    // Hz
    std::atomic<float> lfoDepth     { 0.0f };    // 0..1, 0 = neutral

    // Whole-voice tuning offset (per-track microtuning), applied to every oscillator.
    std::atomic<float> detune       { 0.0f };    // cents

    // Output
    std::atomic<float> gain         { 0.25f };
};

/** Serialise the synth engine params to/from a ValueTree node. Shared by project
    save/load (toValueTree/loadFromTree) and instrument presets so the key set
    stays in one place. */
inline void writeSynthParams (juce::ValueTree& s, const SynthParams& p)
{
    s.setProperty ("wave",       p.waveform.load(), nullptr);
    s.setProperty ("attack",     p.attack.load(), nullptr);
    s.setProperty ("decay",      p.decay.load(), nullptr);
    s.setProperty ("sustain",    p.sustain.load(), nullptr);
    s.setProperty ("release",    p.release.load(), nullptr);
    s.setProperty ("gain",       p.gain.load(), nullptr);
    s.setProperty ("osc2wave",   p.osc2Wave.load(), nullptr);
    s.setProperty ("osc2detune", p.osc2Detune.load(), nullptr);
    s.setProperty ("oscmix",     p.oscMix.load(), nullptr);
    s.setProperty ("sub",        p.subLevel.load(), nullptr);
    s.setProperty ("ftype",      p.filterType.load(), nullptr);
    s.setProperty ("cutoff",     p.cutoff.load(), nullptr);
    s.setProperty ("reso",       p.resonance.load(), nullptr);
    s.setProperty ("fenvamt",    p.filterEnvAmt.load(), nullptr);
    s.setProperty ("fattack",    p.fAttack.load(), nullptr);
    s.setProperty ("fdecay",     p.fDecay.load(), nullptr);
    s.setProperty ("fsustain",   p.fSustain.load(), nullptr);
    s.setProperty ("frelease",   p.fRelease.load(), nullptr);
    s.setProperty ("lfotarget",  p.lfoTarget.load(), nullptr);
    s.setProperty ("lforate",    p.lfoRate.load(), nullptr);
    s.setProperty ("lfodepth",   p.lfoDepth.load(), nullptr);
    s.setProperty ("detune",     p.detune.load(), nullptr);
}

inline void readSynthParams (const juce::ValueTree& s, SynthParams& p)
{
    p.waveform.store     ((int)   s.getProperty ("wave", 1));
    p.attack.store       ((float) (double) s.getProperty ("attack", 0.01));
    p.decay.store        ((float) (double) s.getProperty ("decay", 0.15));
    p.sustain.store      ((float) (double) s.getProperty ("sustain", 0.7));
    p.release.store      ((float) (double) s.getProperty ("release", 0.25));
    p.gain.store         ((float) (double) s.getProperty ("gain", 0.25));
    p.osc2Wave.store     ((int)   s.getProperty ("osc2wave", 1));
    p.osc2Detune.store   ((float) (double) s.getProperty ("osc2detune", 0.0));
    p.oscMix.store       ((float) (double) s.getProperty ("oscmix", 0.0));
    p.subLevel.store     ((float) (double) s.getProperty ("sub", 0.0));
    p.filterType.store   ((int)   s.getProperty ("ftype", 0));
    p.cutoff.store       ((float) (double) s.getProperty ("cutoff", 20000.0));
    p.resonance.store    ((float) (double) s.getProperty ("reso", 0.7));
    p.filterEnvAmt.store ((float) (double) s.getProperty ("fenvamt", 0.0));
    p.fAttack.store      ((float) (double) s.getProperty ("fattack", 0.01));
    p.fDecay.store       ((float) (double) s.getProperty ("fdecay", 0.20));
    p.fSustain.store     ((float) (double) s.getProperty ("fsustain", 0.60));
    p.fRelease.store     ((float) (double) s.getProperty ("frelease", 0.30));
    p.lfoTarget.store    ((int)   s.getProperty ("lfotarget", 0));
    p.lfoRate.store      ((float) (double) s.getProperty ("lforate", 5.0));
    p.lfoDepth.store     ((float) (double) s.getProperty ("lfodepth", 0.0));
    p.detune.store       ((float) (double) s.getProperty ("detune", 0.0));
}

/** Marker sound: our single voice type plays every note on every channel. */
struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote    (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

/** A polyphonic voice: two naive oscillators (+ optional sub), a per-voice
    resonant filter driven by its own ADSR, and an LFO routable to pitch, filter
    cutoff or amplitude. Non-band-limited oscillators — fine for this engine. */
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
        phase1 = phase2 = phaseSub = 0.0;
        level  = velocity;
        baseFreq = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber)
                     * std::pow (2.0, (double) params.detune.load() / 1200.0);   // per-track cents offset

        adsr.setSampleRate (getSampleRate());
        fenv.setSampleRate (getSampleRate());
        updateEnvelopeParameters();
        adsr.noteOn();
        fenv.noteOn();

        ic1eq = ic2eq = 0.0f;   // reset filter state per note
        lfoPhase = 0.0;
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
            fenv.noteOff();
        }
        else
        {
            adsr.reset();
            fenv.reset();
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

        updateEnvelopeParameters();

        const int   wf1    = params.waveform.load();
        const int   wf2    = params.osc2Wave.load();
        const float detune = params.osc2Detune.load();
        const float mix    = params.oscMix.load();
        const float sub    = params.subLevel.load();
        const float g      = params.gain.load();

        const int   ftype  = params.filterType.load();
        const float cutoff = params.cutoff.load();
        const float reso   = juce::jmax (0.5f, params.resonance.load());
        const float fAmt   = params.filterEnvAmt.load();

        const int   lTgt   = params.lfoTarget.load();
        const float lRate  = params.lfoRate.load();
        const float lDepth = params.lfoDepth.load();

        const double sr    = getSampleRate();
        const double d1    = baseFreq / sr;
        const double d2    = baseFreq * std::pow (2.0, detune / 1200.0) / sr;
        const double dSub  = baseFreq * 0.5 / sr;
        const double lfoInc = lRate / sr;

        const bool   filterActive = (cutoff < 19000.0f) || (fAmt > 0.001f)
                                   || (lTgt == 1 && lDepth > 0.001f) || (reso > 0.8f);

        for (int i = 0; i < numSamples; ++i)
        {
            const float aenv = adsr.getNextSample();
            const float fe   = fenv.getNextSample();

            // LFO (sine, [-1,1]).
            const float lfo = lDepth > 0.0f
                ? (float) std::sin (lfoPhase * juce::MathConstants<double>::twoPi) : 0.0f;
            lfoPhase += lfoInc; if (lfoPhase >= 1.0) lfoPhase -= 1.0;

            // Pitch modulation (vibrato): ±1 semitone at full depth.
            const double pitchMul = (lTgt == 0 && lDepth > 0.0f)
                ? std::pow (2.0, (lDepth * lfo) / 12.0) : 1.0;

            // Oscillators.
            const float o1  = oscillator (phase1, wf1);
            const float o2  = oscillator (phase2, wf2);
            float sample    = (o1 * (1.0f - mix) + o2 * mix);
            if (sub > 0.0f)
                sample += sub * (float) std::sin (phaseSub * juce::MathConstants<double>::twoPi);

            // Filter (Cytomic TPT state-variable), cutoff swept by env + LFO.
            if (filterActive)
            {
                double fc = cutoff;
                if (fAmt > 0.0f)                 fc *= std::pow (2.0, fAmt * fe);
                if (lTgt == 1 && lDepth > 0.0f)  fc *= std::pow (2.0, 2.0 * lDepth * lfo);
                fc = juce::jlimit (20.0, sr * 0.49, fc);

                const float gCoef = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
                const float k  = 1.0f / reso;
                const float a1 = 1.0f / (1.0f + gCoef * (gCoef + k));
                const float a2 = gCoef * a1;
                const float a3 = gCoef * a2;

                const float v3 = sample - ic2eq;
                const float v1 = a1 * ic1eq + a2 * v3;
                const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0f * v1 - ic1eq;
                ic2eq = 2.0f * v2 - ic2eq;

                switch (ftype)
                {
                    case 1:  sample = sample - k * v1 - v2; break;   // high-pass
                    case 2:  sample = v1;                   break;   // band-pass
                    default: sample = v2;                   break;   // low-pass
                }
            }

            // Amplitude (+ tremolo).
            float amp = aenv * level * g;
            if (lTgt == 2 && lDepth > 0.0f)
                amp *= 1.0f - lDepth * 0.5f * (1.0f - lfo);   // stays in [1-depth, 1]

            const float out = sample * amp;
            for (int ch = outputBuffer.getNumChannels(); --ch >= 0;)
                outputBuffer.addSample (ch, startSample + i, out);

            phase1   += d1 * pitchMul; if (phase1   >= 1.0) phase1   -= 1.0;
            phase2   += d2 * pitchMul; if (phase2   >= 1.0) phase2   -= 1.0;
            phaseSub += dSub * pitchMul; if (phaseSub >= 1.0) phaseSub -= 1.0;
        }

        if (! adsr.isActive())
            clearCurrentNote();
    }

private:
    void updateEnvelopeParameters()
    {
        juce::ADSR::Parameters p;
        p.attack  = params.attack.load();
        p.decay   = params.decay.load();
        p.sustain = params.sustain.load();
        p.release = params.release.load();
        adsr.setParameters (p);

        juce::ADSR::Parameters f;
        f.attack  = params.fAttack.load();
        f.decay   = params.fDecay.load();
        f.sustain = params.fSustain.load();
        f.release = params.fRelease.load();
        fenv.setParameters (f);
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
    juce::ADSR   adsr;      // amplitude
    juce::ADSR   fenv;      // filter cutoff
    double       phase1   { 0.0 };
    double       phase2   { 0.0 };
    double       phaseSub { 0.0 };
    double       baseFreq { 440.0 };
    double       lfoPhase { 0.0 };
    float        level    { 1.0f };
    float        ic1eq    { 0.0f };   // SVF state
    float        ic2eq    { 0.0f };
};
