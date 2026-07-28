// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>
#include "Effect.h"
#include "StereoWiden.h"
#include "AllpassPhaser.h"
#include "Biquad.h"
#include "EffectSync.h"

// ---------------------------------------------------------------------------
// Gain
// ---------------------------------------------------------------------------
class GainFx : public Effect
{
public:
    void prepare (double, int, int) override {}

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        b.applyGain (juce::Decibels::decibelsToGain (gainDb.load()));
    }

    juce::String name() const override { return "Gain"; }

    std::vector<EffectParam> parameters() override
    {
        return { { "Gain dB", -24.0f, 24.0f, 0.0f,
                   [this] { return gainDb.load(); },
                   [this] (float v) { gainDb.store (v); } } };
    }

private:
    std::atomic<float> gainDb { 0.0f };
};

// ---------------------------------------------------------------------------
// Filter (TPT state-variable, low-pass / high-pass)
// ---------------------------------------------------------------------------
class FilterFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = sampleRate; reset(); }
    void reset() override { state = {}; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;

        const float fc = juce::jlimit (20.0f, (float) (sr * 0.45), cutoff.load());
        const float Q  = 0.5f + juce::jlimit (0.0f, 1.0f, resonance.load()) * 7.5f;
        const float g  = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
        const float k  = 1.0f / Q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const int   hp = (int) std::lround (mode.load()); // 0 LP, 1 HP

        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            auto& s = state[(size_t) ch];
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x  = d[i];
                const float v3 = x - s.ic2;
                const float v1 = a1 * s.ic1 + a2 * v3;
                const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1;
                s.ic2 = 2.0f * v2 - s.ic2;
                d[i] = (hp == 1) ? (x - k * v1 - v2) : v2;
            }
        }
    }

    juce::String name() const override { return "Filter"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Cutoff", 20.0f, 18000.0f, 2000.0f,
              [this] { return cutoff.load(); }, [this] (float v) { cutoff.store (v); } },
            { "Reso",   0.0f, 1.0f, 0.3f,
              [this] { return resonance.load(); }, [this] (float v) { resonance.store (v); } },
            { "HP?",    0.0f, 1.0f, 0.0f,
              [this] { return mode.load(); }, [this] (float v) { mode.store (v); } }
        };
    }

private:
    struct State { float ic1 { 0.0f }, ic2 { 0.0f }; };
    double sr { 44100.0 };
    std::atomic<float> cutoff { 2000.0f }, resonance { 0.3f }, mode { 0.0f };
    std::array<State, 2> state;
};

// ---------------------------------------------------------------------------
// Delay (stereo, feedback, wet/dry)
// ---------------------------------------------------------------------------
class DelayFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override
    {
        sr = sampleRate;
        bufLen = (int) (sr * 2.0) + 4;   // up to 2 s
        for (auto& b : buffers) b.assign ((size_t) bufLen, 0.0f);
        writePos = 0;
    }

    void reset() override
    {
        for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0;
    }

    void setTempo (double b) override { bpm = juce::jmax (1.0, b); }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load() || bufLen <= 4) return;

        // Sync > 0 sets the delay length in beats (tempo-synced); otherwise Time ms is used.
        const float syncBt = syncBeats.load();
        const float delMs  = syncBt > 0.0f ? syncBt * (float) (60000.0 / bpm) : timeMs.load();
        const float base = juce::jlimit (1.0f, (float) (bufLen - 4), (delMs / 1000.0f) * (float) sr);
        const float fb = juce::jlimit (0.0f, 0.95f, feedback.load());
        const float mx = juce::jlimit (0.0f, 1.0f, mix.load());

        auto rd = [this] (std::vector<float>& buf, float pos)
        {
            while (pos < 0.0f) pos += (float) bufLen;
            const int i0 = ((int) pos) % bufLen;
            const float fr = pos - (float) ((int) pos);
            const int i1 = (i0 + 1) % bufLen;
            return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
        };

        if (b.getNumChannels() < 2)   // mono fallback
        {
            auto* d0 = b.getWritePointer (0);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float del = rd (buffers[0], (float) writePos - base);
                const float in = d0[i];
                buffers[0][(size_t) writePos] = in + del * fb;
                d0[i] = in * (1.0f - mx) + del * mx;
                writePos = (writePos + 1) % bufLen;
            }
            return;
        }

        // Stereo ping-pong: cross-fed feedback + a dotted L offset for width.
        const float dR = base;
        const float dL = juce::jlimit (1.0f, (float) (bufLen - 4), base * 1.5f);
        auto* L = b.getWritePointer (0);
        auto* R = b.getWritePointer (1);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float dl = rd (buffers[0], (float) writePos - dL);
            const float dr = rd (buffers[1], (float) writePos - dR);
            const float inL = L[i], inR = R[i];
            buffers[0][(size_t) writePos] = inL + dr * fb;   // right feeds left
            buffers[1][(size_t) writePos] = inR + dl * fb;   // left feeds right
            L[i] = inL * (1.0f - mx) + dl * mx;
            R[i] = inR * (1.0f - mx) + dr * mx;
            writePos = (writePos + 1) % bufLen;
        }
    }

    juce::String name() const override { return "Delay"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Time ms", 10.0f, 1000.0f, 300.0f,
              [this] { return timeMs.load(); }, [this] (float v) { timeMs.store (v); } },
            { "Sync bt", 0.0f, 4.0f, 0.0f,                     // 0 = free (Time ms); e.g. 0.5=1/8, 1=1/4
              [this] { return syncBeats.load(); }, [this] (float v) { syncBeats.store (v); } },
            { "Feedbk", 0.0f, 0.95f, 0.35f,
              [this] { return feedback.load(); }, [this] (float v) { feedback.store (v); } },
            { "Mix",    0.0f, 1.0f, 0.3f,
              [this] { return mix.load(); }, [this] (float v) { mix.store (v); } }
        };
    }

private:
    double sr { 44100.0 }, bpm { 120.0 };
    int    bufLen { 0 }, writePos { 0 };
    std::array<std::vector<float>, 2> buffers;
    std::atomic<float> timeMs { 300.0f }, syncBeats { 0.0f }, feedback { 0.35f }, mix { 0.3f };
};

// ---------------------------------------------------------------------------
// Reverb (wraps juce::Reverb)
// ---------------------------------------------------------------------------
class ReverbFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { reverb.setSampleRate (sampleRate); }
    void reset() override { reverb.reset(); }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;

        juce::Reverb::Parameters p;
        p.roomSize   = room.load();
        p.damping    = damp.load();
        p.wetLevel   = wet.load();
        p.dryLevel   = 1.0f - wet.load();
        p.width      = 1.0f;
        p.freezeMode = 0.0f;
        reverb.setParameters (p);

        if (b.getNumChannels() >= 2)
            reverb.processStereo (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
        else if (b.getNumChannels() == 1)
            reverb.processMono (b.getWritePointer (0), b.getNumSamples());
    }

    juce::String name() const override { return "Reverb"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Room", 0.0f, 1.0f, 0.5f,
              [this] { return room.load(); }, [this] (float v) { room.store (v); } },
            { "Damp", 0.0f, 1.0f, 0.5f,
              [this] { return damp.load(); }, [this] (float v) { damp.store (v); } },
            { "Wet",  0.0f, 1.0f, 0.33f,
              [this] { return wet.load(); }, [this] (float v) { wet.store (v); } }
        };
    }

private:
    juce::Reverb reverb;
    std::atomic<float> room { 0.5f }, damp { 0.5f }, wet { 0.33f };
};

/** A simple brickwall peak limiter (instant attack, exponential release) — a
    safety net for the master bus so summed tracks don't clip at 0 dBFS. */
class LimiterFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = (float) sampleRate; reset(); }
    void reset() override { env = 1.0f; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float thr = juce::Decibels::decibelsToGain (threshDb.load());
        const float relCoef = std::exp (-1.0f / (juce::jmax (1.0f, release.load()) * 0.001f * sr));
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < ch; ++c) peak = juce::jmax (peak, std::abs (b.getSample (c, i)));
            const float target = peak > thr ? thr / peak : 1.0f;
            env = target < env ? target : target + (env - target) * relCoef;   // attack instant, release smooth
            for (int c = 0; c < ch; ++c) b.getWritePointer (c)[i] *= env;
        }
    }

    juce::String name() const override { return "Limiter"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Thresh dB", -24.0f, 0.0f, -1.0f,
              [this] { return threshDb.load(); }, [this] (float v) { threshDb.store (v); } },
            { "Release ms", 1.0f, 500.0f, 80.0f,
              [this] { return release.load(); }, [this] (float v) { release.store (v); } }
        };
    }

private:
    std::atomic<float> threshDb { -1.0f }, release { 80.0f };
    float env { 1.0f }, sr { 44100.0f };
};

// ---------------------------------------------------------------------------
// Bitcrusher (bit-depth reduction + sample-rate decimation, wet/dry)
// ---------------------------------------------------------------------------
class BitcrusherFx : public Effect
{
public:
    void prepare (double, int, int) override { reset(); }
    void reset() override { for (auto& h : hold) h = 0.0f; counter = 0; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const int   bits = juce::jlimit (1, 16, (int) std::lround (bitsParam.load()));
        const float step = 1.0f / (float) (1 << (bits - 1));        // 2^bits levels over [-1,1]
        const int   ds   = juce::jmax (1, (int) std::lround (downsample.load()));
        const float mix  = juce::jlimit (0.0f, 1.0f, wet.load());
        const int   n = b.getNumSamples(), ch = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const bool sampleNow = (counter % ds) == 0;
            for (int c = 0; c < ch; ++c)
            {
                auto* d = b.getWritePointer (c);
                if (sampleNow) hold[(size_t) c] = std::round (d[i] / step) * step;   // S&H + quantise
                d[i] = d[i] + mix * (hold[(size_t) c] - d[i]);                       // dry + wet
            }
            ++counter;
        }
    }

    juce::String name() const override { return "Bitcrusher"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Bits",       1.0f, 16.0f, 8.0f,  [this] { return bitsParam.load(); }, [this] (float v) { bitsParam.store (v); } },
            { "Downsample", 1.0f, 50.0f, 1.0f,  [this] { return downsample.load(); }, [this] (float v) { downsample.store (v); } },
            { "Mix",        0.0f, 1.0f, 1.0f,   [this] { return wet.load(); },        [this] (float v) { wet.store (v); } }
        };
    }

private:
    std::atomic<float> bitsParam { 8.0f }, downsample { 1.0f }, wet { 1.0f };
    std::array<float, 2> hold { { 0.0f, 0.0f } };
    int counter { 0 };
};

// ---------------------------------------------------------------------------
// Compressor (peak-detected, soft envelope, makeup gain)
// ---------------------------------------------------------------------------
class CompressorFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = (float) sampleRate; reset(); }
    void reset() override { env = 0.0f; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float thr   = threshDb.load();
        const float ratio = juce::jmax (1.0f, ratioParam.load());
        const float atCoef = std::exp (-1.0f / (juce::jmax (0.1f, attackMs.load())  * 0.001f * sr));
        const float rlCoef = std::exp (-1.0f / (juce::jmax (1.0f, releaseMs.load()) * 0.001f * sr));
        const float makeup = juce::Decibels::decibelsToGain (makeupDb.load());
        const int n = b.getNumSamples(), ch = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < ch; ++c) peak = juce::jmax (peak, std::abs (b.getSample (c, i)));
            env = peak > env ? atCoef * (env - peak) + peak : rlCoef * (env - peak) + peak;   // detector
            const float envDb = juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, env));
            const float overDb = envDb - thr;
            const float grDb = overDb > 0.0f ? overDb * (1.0f - 1.0f / ratio) : 0.0f;         // reduction (dB)
            const float g = makeup * juce::Decibels::decibelsToGain (-grDb);
            for (int c = 0; c < ch; ++c) b.getWritePointer (c)[i] *= g;
        }
    }

    juce::String name() const override { return "Compressor"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Thresh dB",  -48.0f, 0.0f, -18.0f, [this] { return threshDb.load(); },   [this] (float v) { threshDb.store (v); } },
            { "Ratio",      1.0f, 20.0f, 4.0f,    [this] { return ratioParam.load(); }, [this] (float v) { ratioParam.store (v); } },
            { "Attack ms",  0.1f, 100.0f, 10.0f,  [this] { return attackMs.load(); },   [this] (float v) { attackMs.store (v); } },
            { "Release ms", 10.0f, 1000.0f, 120.0f, [this] { return releaseMs.load(); }, [this] (float v) { releaseMs.store (v); } },
            { "Makeup dB",  0.0f, 24.0f, 0.0f,    [this] { return makeupDb.load(); },   [this] (float v) { makeupDb.store (v); } }
        };
    }

private:
    std::atomic<float> threshDb { -18.0f }, ratioParam { 4.0f }, attackMs { 10.0f }, releaseMs { 120.0f }, makeupDb { 0.0f };
    float env { 0.0f }, sr { 44100.0f };
};

// ---------------------------------------------------------------------------
// Noise Gate — a downward gate (the dynamics complement to the limiter/compressor):
// when the signal level drops below Thresh dB the gain falls to Range dB (a floor, e.g.
// -60 dB ≈ silence), attenuating hum/hiss/bleed in the gaps; above threshold it passes
// at unity. The gate gain smooths toward its target with Attack (opening) / Release
// (closing) time constants. Starts closed; reset() clears the state for reproducible
// bounces.
// ---------------------------------------------------------------------------
class NoiseGateFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = (float) sampleRate; reset(); }
    void reset() override { gain = 0.0f; }              // closed until the signal opens it

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float thr    = threshDb.load();
        const float floorG = juce::Decibels::decibelsToGain (rangeDb.load());   // gain when closed
        const float atCoef = std::exp (-1.0f / (juce::jmax (0.1f, attackMs.load())  * 0.001f * sr));
        const float rlCoef = std::exp (-1.0f / (juce::jmax (1.0f, releaseMs.load()) * 0.001f * sr));
        const int n = b.getNumSamples(), ch = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < ch; ++c) peak = juce::jmax (peak, std::abs (b.getSample (c, i)));
            const float lvlDb  = juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, peak));
            const float target = lvlDb >= thr ? 1.0f : floorG;                  // open above threshold, else floor
            const float coef   = target > gain ? atCoef : rlCoef;              // attack when opening, release when closing
            gain = coef * (gain - target) + target;
            for (int c = 0; c < ch; ++c) b.getWritePointer (c)[i] *= gain;
        }
    }

    juce::String name() const override { return "Noise Gate"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Thresh dB",  -80.0f, 0.0f, -40.0f, [this] { return threshDb.load(); },  [this] (float v) { threshDb.store (v); } },
            { "Range dB",   -80.0f, 0.0f, -60.0f, [this] { return rangeDb.load(); },   [this] (float v) { rangeDb.store (v); } },
            { "Attack ms",  0.1f, 100.0f, 1.0f,   [this] { return attackMs.load(); },  [this] (float v) { attackMs.store (v); } },
            { "Release ms", 5.0f, 1000.0f, 80.0f, [this] { return releaseMs.load(); }, [this] (float v) { releaseMs.store (v); } }
        };
    }

private:
    std::atomic<float> threshDb { -40.0f }, rangeDb { -60.0f }, attackMs { 1.0f }, releaseMs { 80.0f };
    float gain { 0.0f }, sr { 44100.0f };
};

// ---------------------------------------------------------------------------
// Auto-wah — an envelope-following resonant low-pass filter (the classic funk
// "envelope filter"). A peak follower on the input drives the cutoff: fc =
// Base Hz · 2^(Range · env), so a loud transient snaps the filter open (bright) and
// it closes again as the note decays (a played "wah"). Range is the octaves of sweep,
// Q the resonance, Release the follower's decay. A shared (mono-summed) detector sweeps
// both channels' filters together. reset() clears the state for reproducible bounces.
// ---------------------------------------------------------------------------
class AutoWahFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = (float) sampleRate; reset(); }
    void reset() override { env = 0.0f; for (auto& s : ic1) s = 0.0f; for (auto& s : ic2) s = 0.0f; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float base  = juce::jlimit (50.0f, 4000.0f, baseHz.load());
        const float range = juce::jlimit (0.0f, 6.0f, rangeOct.load());
        const float q     = juce::jmax (0.5f, resonance.load());
        const float atCoef = std::exp (-1.0f / (0.005f * sr));                          // 5 ms attack
        const float rlCoef = std::exp (-1.0f / (juce::jmax (5.0f, releaseMs.load()) * 0.001f * sr));
        const int n = b.getNumSamples(), ch = juce::jmin (2, b.getNumChannels());
        const float k = 1.0f / q;
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < ch; ++c) peak = juce::jmax (peak, std::abs (b.getSample (c, i)));
            env = peak > env ? atCoef * (env - peak) + peak : rlCoef * (env - peak) + peak;   // follower
            const float e  = juce::jlimit (0.0f, 1.0f, env);
            float fc = base * std::pow (2.0f, range * e);                                // env sweeps the cutoff up
            fc = juce::jlimit (20.0f, sr * 0.49f, fc);
            const float g  = std::tan (juce::MathConstants<float>::pi * fc / sr);        // TPT SVF low-pass
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1, a3 = g * a2;
            for (int c = 0; c < ch; ++c)
            {
                auto* d = b.getWritePointer (c);
                const float v3 = d[i] - ic2[(size_t) c];
                const float v1 = a1 * ic1[(size_t) c] + a2 * v3;
                const float v2 = ic2[(size_t) c] + a2 * ic1[(size_t) c] + a3 * v3;
                ic1[(size_t) c] = 2.0f * v1 - ic1[(size_t) c];
                ic2[(size_t) c] = 2.0f * v2 - ic2[(size_t) c];
                d[i] = v2;                                                               // low-pass output
            }
        }
    }

    juce::String name() const override { return "Auto-wah"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Base Hz",  50.0f, 2000.0f, 300.0f, [this] { return baseHz.load(); },    [this] (float v) { baseHz.store (v); } },
            { "Range oct", 0.0f, 6.0f, 3.5f,      [this] { return rangeOct.load(); },  [this] (float v) { rangeOct.store (v); } },
            { "Q",        0.5f, 10.0f, 3.0f,      [this] { return resonance.load(); }, [this] (float v) { resonance.store (v); } },
            { "Release ms", 5.0f, 1000.0f, 120.0f, [this] { return releaseMs.load(); }, [this] (float v) { releaseMs.store (v); } }
        };
    }

private:
    std::atomic<float> baseHz { 300.0f }, rangeOct { 3.5f }, resonance { 3.0f }, releaseMs { 120.0f };
    float env { 0.0f }, sr { 44100.0f };
    std::array<float, 2> ic1 { 0.0f, 0.0f }, ic2 { 0.0f, 0.0f };
};

// ---------------------------------------------------------------------------
// Ring modulator — multiply the signal by a sine carrier at Freq Hz, producing
// inharmonic sum/difference tones (a metallic, bell-like, or robotic timbre). Unlike
// the filter/LFO/dynamics effects this is a nonlinear amplitude multiplication:
// out = in·sin(2π·Freq·t), blended with the dry signal by Mix (0 = dry, 1 = fully ring-
// modulated). Both channels share one carrier phase. reset() zeroes the phase so offline
// bounces are reproducible.
// ---------------------------------------------------------------------------
class RingModFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = (float) sampleRate; phase = 0.0f; }
    void reset() override { phase = 0.0f; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float mx = juce::jlimit (0.0f, 1.0f, mix.load());
        if (mx <= 0.0f) return;                                          // identity when fully dry
        const float freq = juce::jlimit (1.0f, 8000.0f, freqHz.load());
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float inc   = twoPi * freq / sr;
        const int nch = b.getNumChannels();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float carrier = std::sin (phase);
            for (int c = 0; c < nch; ++c)
            {
                auto* d = b.getWritePointer (c);
                d[i] = d[i] * (1.0f - mx) + (d[i] * carrier) * mx;       // dry/wet blend
            }
            phase += inc; if (phase >= twoPi) phase -= twoPi;
        }
    }

    juce::String name() const override { return "Ring Mod"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Freq",  1.0f, 4000.0f, 200.0f, [this] { return freqHz.load(); }, [this] (float v) { freqHz.store (v); } },
            { "Mix",   0.0f, 1.0f, 1.0f,       [this] { return mix.load(); },    [this] (float v) { mix.store (v); } }
        };
    }

private:
    std::atomic<float> freqHz { 200.0f }, mix { 1.0f };
    float phase { 0.0f }, sr { 44100.0f };
};

// ---------------------------------------------------------------------------
// Parametric EQ (single peaking band, RBJ biquad)
// ---------------------------------------------------------------------------
// Three-band EQ: a low shelf, a mid peaking band, and a high shelf, chained per channel.
// The mid band keeps the original Freq/Gain dB/Q names so projects that stored the old
// single-band EQ still load (the shelves default flat). RBJ coefficients live in Biquad.h.
class EqFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = sampleRate; reset(); }
    void reset() override { for (auto& band : st) for (auto& s : band) s = {}; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const BiquadCoeffs c[3] = {
            eqLowShelf  (juce::jlimit (20.0f, 2000.0f, lowFreq.load()),   juce::jlimit (-24.0f, 24.0f, lowDb.load()),  sr),
            eqPeak      (juce::jlimit (20.0f, 18000.0f, freq.load()),     juce::jlimit (-24.0f, 24.0f, gainDb.load()),
                         juce::jmax (0.1f, q.load()), sr),
            eqHighShelf (juce::jlimit (1000.0f, 20000.0f, highFreq.load()), juce::jlimit (-24.0f, 24.0f, highDb.load()), sr)
        };
        const int n = b.getNumSamples(), ch = juce::jmin (2, b.getNumChannels());
        for (int chan = 0; chan < ch; ++chan)
        {
            auto* d = b.getWritePointer (chan);
            for (int i = 0; i < n; ++i)
            {
                double x = d[i];
                for (int band = 0; band < 3; ++band)               // low shelf -> peak -> high shelf
                {
                    auto& s = st[(size_t) band][(size_t) chan];
                    const double out = c[band].b0 * x + s.z1;       // transposed direct form II
                    s.z1 = c[band].b1 * x - c[band].a1 * out + s.z2;
                    s.z2 = c[band].b2 * x - c[band].a2 * out;
                    x = out;
                }
                d[i] = (float) x;
            }
        }
    }

    juce::String name() const override { return "EQ"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Low Freq",  20.0f, 2000.0f, 120.0f,   [this] { return lowFreq.load(); },  [this] (float v) { lowFreq.store (v); } },
            { "Low dB",    -24.0f, 24.0f, 0.0f,      [this] { return lowDb.load(); },    [this] (float v) { lowDb.store (v); } },
            { "Freq",      20.0f, 18000.0f, 1000.0f, [this] { return freq.load(); },     [this] (float v) { freq.store (v); } },
            { "Gain dB",   -24.0f, 24.0f, 0.0f,      [this] { return gainDb.load(); },   [this] (float v) { gainDb.store (v); } },
            { "Q",         0.1f, 10.0f, 1.0f,        [this] { return q.load(); },        [this] (float v) { q.store (v); } },
            { "High Freq", 1000.0f, 20000.0f, 6000.0f, [this] { return highFreq.load(); }, [this] (float v) { highFreq.store (v); } },
            { "High dB",   -24.0f, 24.0f, 0.0f,      [this] { return highDb.load(); },   [this] (float v) { highDb.store (v); } }
        };
    }

private:
    struct St { double z1 { 0 }, z2 { 0 }; };
    std::atomic<float> lowFreq { 120.0f }, lowDb { 0.0f },
                       freq { 1000.0f }, gainDb { 0.0f }, q { 1.0f },
                       highFreq { 6000.0f }, highDb { 0.0f };
    std::array<std::array<St, 2>, 3> st;   // [band][channel]
    double sr { 44100.0 };
};

// ---------------------------------------------------------------------------
// Waveshaper (tanh soft-clip drive, wet/dry)
// ---------------------------------------------------------------------------
class WaveshaperFx : public Effect
{
public:
    void prepare (double, int, int) override {}

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float drive = juce::jmax (1.0f, driveParam.load());
        const float mix   = juce::jlimit (0.0f, 1.0f, wet.load());
        const float norm  = std::tanh (drive);                     // keep full-scale near unity
        const int n = b.getNumSamples(), ch = juce::jmin (2, b.getNumChannels());
        for (int c = 0; c < ch; ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                const float shaped = std::tanh (drive * x) / norm;
                d[i] = x + mix * (shaped - x);
            }
        }
    }

    juce::String name() const override { return "Waveshaper"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Drive", 1.0f, 50.0f, 1.0f, [this] { return driveParam.load(); }, [this] (float v) { driveParam.store (v); } },
            { "Mix",   0.0f, 1.0f, 1.0f,  [this] { return wet.load(); },        [this] (float v) { wet.store (v); } }
        };
    }

private:
    std::atomic<float> driveParam { 1.0f }, wet { 1.0f };
};

// ---------------------------------------------------------------------------
// Stereo widener (mid/side). Width 0 = mono, 1 = unchanged, 2 = double-wide.
// mid = (L+R)/2 keeps the centre; the side (L-R)/2 is scaled by Width, so the
// effect is a no-op on a mono/centred signal and cannot shift the mono sum.
// ---------------------------------------------------------------------------
class StereoWidenerFx : public Effect
{
public:
    void prepare (double, int, int) override {}

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load() || b.getNumChannels() < 2) return;   // nothing to widen in mono
        const float w = juce::jlimit (0.0f, 2.0f, width.load());
        const int n = b.getNumSamples();
        auto* L = b.getWritePointer (0);
        auto* R = b.getWritePointer (1);
        for (int i = 0; i < n; ++i)
            widenSample (L[i], R[i], w);
    }

    juce::String name() const override { return "Stereo Widener"; }

    std::vector<EffectParam> parameters() override
    {
        return { { "Width", 0.0f, 2.0f, 1.0f,
                   [this] { return width.load(); },
                   [this] (float v) { width.store (v); } } };
    }

private:
    std::atomic<float> width { 1.0f };
};

// ---------------------------------------------------------------------------
// Tremolo — periodic amplitude modulation. A sine LFO scales the gain between
// 1 and (1 - Depth) at Rate Hz (or tempo-synced when Sync bt > 0, reusing the same
// beats->Hz law as the modulation effects). Both channels share one phase (classic
// tremolo, not auto-pan). Depth 0 = identity (a true passthrough); Depth 1 fully
// gates the trough to silence. Stateless apart from the LFO phase, so reset() makes
// offline bounces reproducible.
// ---------------------------------------------------------------------------
class TremoloFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = sampleRate; phase = 0.0f; }
    void reset() override { phase = 0.0f; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float depth = juce::jlimit (0.0f, 1.0f, depthAmt.load());
        if (depth <= 0.0f) return;                                    // identity: leave the signal untouched
        const float rate  = effectSyncedRate (bpm, syncBeats.load(), rateHz.load());   // tempo-sync when Sync bt > 0
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float inc   = twoPi * rate / (float) sr;
        const int nch = b.getNumChannels();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            // g in [1-depth, 1]: full level at phase 0, trough half a cycle later.
            const float g = 1.0f - depth * (0.5f + 0.5f * std::sin (phase));
            for (int c = 0; c < nch; ++c) b.getWritePointer (c)[i] *= g;
            phase += inc; if (phase >= twoPi) phase -= twoPi;
        }
    }

    void setTempo (double b) override { bpm = juce::jmax (1.0, b); }

    juce::String name() const override { return "Tremolo"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Rate",  0.05f, 20.0f, 5.0f, [this] { return rateHz.load(); },    [this] (float v) { rateHz.store (v); } },
            { "Sync bt", 0.0f, 4.0f, 0.0f, [this] { return syncBeats.load(); }, [this] (float v) { syncBeats.store (v); } },
            { "Depth", 0.0f, 1.0f, 0.5f,   [this] { return depthAmt.load(); },  [this] (float v) { depthAmt.store (v); } }
        };
    }

private:
    double sr { 44100.0 }, bpm { 120.0 };
    float  phase { 0.0f };
    std::atomic<float> rateHz { 5.0f }, depthAmt { 0.5f }, syncBeats { 0.0f };
};

// ---------------------------------------------------------------------------
// Auto-pan — the stereo companion to the tremolo: one sine LFO drives the two
// channels in ANTIPHASE, so the signal sweeps L<->R at Rate Hz (or tempo-synced
// when Sync bt > 0). Left gain = 1 - Depth*(0.5+0.5*sin), right = 1 - Depth*(0.5-0.5*sin);
// Depth 0 = identity, Depth 1 = full ping-pong (one channel silent at each extreme).
// Needs a stereo buffer; mono passes through. reset() zeroes the phase for reproducible
// bounces.
// ---------------------------------------------------------------------------
class AutoPanFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = sampleRate; phase = 0.0f; }
    void reset() override { phase = 0.0f; }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load() || b.getNumChannels() < 2) return;        // nothing to pan in mono
        const float depth = juce::jlimit (0.0f, 1.0f, depthAmt.load());
        if (depth <= 0.0f) return;                                    // identity
        const float rate  = effectSyncedRate (bpm, syncBeats.load(), rateHz.load());   // tempo-sync when Sync bt > 0
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float inc   = twoPi * rate / (float) sr;
        auto* L = b.getWritePointer (0);
        auto* R = b.getWritePointer (1);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float s = std::sin (phase);
            L[i] *= 1.0f - depth * (0.5f + 0.5f * s);                 // channels move in antiphase
            R[i] *= 1.0f - depth * (0.5f - 0.5f * s);
            phase += inc; if (phase >= twoPi) phase -= twoPi;
        }
    }

    void setTempo (double bt) override { bpm = juce::jmax (1.0, bt); }

    juce::String name() const override { return "Auto-pan"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Rate",  0.05f, 20.0f, 2.0f, [this] { return rateHz.load(); },    [this] (float v) { rateHz.store (v); } },
            { "Sync bt", 0.0f, 4.0f, 0.0f, [this] { return syncBeats.load(); }, [this] (float v) { syncBeats.store (v); } },
            { "Depth", 0.0f, 1.0f, 0.7f,   [this] { return depthAmt.load(); },  [this] (float v) { depthAmt.store (v); } }
        };
    }

private:
    double sr { 44100.0 }, bpm { 120.0 };
    float  phase { 0.0f };
    std::atomic<float> rateHz { 2.0f }, depthAmt { 0.7f }, syncBeats { 0.0f };
};

// ---------------------------------------------------------------------------
// Chorus — a short LFO-modulated delay (no feedback) mixed with the dry signal.
// The delay time sweeps base..base+Depth ms at Rate Hz; the right channel's LFO is a
// quarter-cycle ahead for stereo width. Mix 0 = dry (identity), 1 = fully wet.
// ---------------------------------------------------------------------------
class ChorusFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override
    {
        sr = sampleRate;
        bufLen = (int) (sr * 0.05) + 4;   // 50 ms delay line is plenty for a chorus
        for (auto& b : buffers) b.assign ((size_t) bufLen, 0.0f);
        writePos = 0; phase = 0.0f;
    }

    void reset() override
    {
        for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0; phase = 0.0f;
    }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load() || bufLen <= 4) return;
        const float rate  = effectSyncedRate (bpm, syncBeats.load(), rateHz.load());   // tempo-sync when Sync bt > 0
        const float depth = juce::jlimit (0.0f, 10.0f, depthMs.load());
        const float mx    = juce::jlimit (0.0f, 1.0f, mix.load());
        const float baseMs = 12.0f;                                   // centre delay
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float inc   = twoPi * rate / (float) sr;

        auto rd = [this] (std::vector<float>& buf, float pos)
        {
            while (pos < 0.0f) pos += (float) bufLen;
            const int i0 = ((int) pos) % bufLen;
            const float fr = pos - (float) ((int) pos);
            const int i1 = (i0 + 1) % bufLen;
            return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
        };

        const int nch = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float sweepL = (baseMs + depth * (0.5f + 0.5f * std::sin (phase))) * 0.001f * (float) sr;
            const float sweepR = (baseMs + depth * (0.5f + 0.5f * std::sin (phase + twoPi * 0.25f))) * 0.001f * (float) sr;
            for (int c = 0; c < nch; ++c)
            {
                auto* d = b.getWritePointer (c);
                const float in = d[i];
                buffers[(size_t) c][(size_t) writePos] = in;          // no feedback (clean chorus)
                const float del = rd (buffers[(size_t) c], (float) writePos - (c == 0 ? sweepL : sweepR));
                d[i] = in * (1.0f - mx) + del * mx;
            }
            writePos = (writePos + 1) % bufLen;
            phase += inc; if (phase >= twoPi) phase -= twoPi;
        }
    }

    void setTempo (double b) override { bpm = juce::jmax (1.0, b); }

    juce::String name() const override { return "Chorus"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Rate",  0.05f, 8.0f, 1.0f,  [this] { return rateHz.load(); },  [this] (float v) { rateHz.store (v); } },
            { "Sync bt", 0.0f, 4.0f, 0.0f, [this] { return syncBeats.load(); }, [this] (float v) { syncBeats.store (v); } },
            { "Depth", 0.0f, 10.0f, 3.0f,  [this] { return depthMs.load(); }, [this] (float v) { depthMs.store (v); } },
            { "Mix",   0.0f, 1.0f, 0.5f,   [this] { return mix.load(); },     [this] (float v) { mix.store (v); } }
        };
    }

private:
    double sr { 44100.0 }, bpm { 120.0 };
    int    bufLen { 0 }, writePos { 0 };
    float  phase { 0.0f };
    std::array<std::vector<float>, 2> buffers;
    std::atomic<float> rateHz { 1.0f }, depthMs { 3.0f }, mix { 0.5f }, syncBeats { 0.0f };
};

// ---------------------------------------------------------------------------
// Flanger — a very short LFO-modulated delay (sub-ms..~5 ms) with feedback, mixed
// with the dry signal. The short swept delay plus feedback builds the resonant
// comb-filter "jet" sweep that sets it apart from a chorus. Mix 0 = dry.
// ---------------------------------------------------------------------------
class FlangerFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override
    {
        sr = sampleRate;
        bufLen = (int) (sr * 0.02) + 4;   // 20 ms line is ample for a flanger
        for (auto& b : buffers) b.assign ((size_t) bufLen, 0.0f);
        writePos = 0; phase = 0.0f;
    }

    void reset() override
    {
        for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0; phase = 0.0f;
    }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load() || bufLen <= 4) return;
        const float rate  = effectSyncedRate (bpm, syncBeats.load(), rateHz.load());   // tempo-sync when Sync bt > 0
        const float depth = juce::jlimit (0.0f, 5.0f, depthMs.load());
        const float fb    = juce::jlimit (0.0f, 0.95f, feedback.load());
        const float mx    = juce::jlimit (0.0f, 1.0f, mix.load());
        const float baseMs = 0.5f;                                    // very short centre
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float inc   = twoPi * rate / (float) sr;

        auto rd = [this] (std::vector<float>& buf, float pos)
        {
            while (pos < 0.0f) pos += (float) bufLen;
            const int i0 = ((int) pos) % bufLen;
            const float fr = pos - (float) ((int) pos);
            const int i1 = (i0 + 1) % bufLen;
            return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
        };

        const int nch = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float sweep = (baseMs + depth * (0.5f + 0.5f * std::sin (phase))) * 0.001f * (float) sr;
            for (int c = 0; c < nch; ++c)
            {
                auto* d = b.getWritePointer (c);
                const float in  = d[i];
                const float del = rd (buffers[(size_t) c], (float) writePos - sweep);   // read old, then write
                buffers[(size_t) c][(size_t) writePos] = in + del * fb;                 // feedback into the line
                d[i] = in * (1.0f - mx) + del * mx;
            }
            writePos = (writePos + 1) % bufLen;
            phase += inc; if (phase >= twoPi) phase -= twoPi;
        }
    }

    void setTempo (double b) override { bpm = juce::jmax (1.0, b); }

    juce::String name() const override { return "Flanger"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Rate",   0.05f, 8.0f, 0.5f,  [this] { return rateHz.load(); },   [this] (float v) { rateHz.store (v); } },
            { "Sync bt", 0.0f, 4.0f, 0.0f,  [this] { return syncBeats.load(); }, [this] (float v) { syncBeats.store (v); } },
            { "Depth",  0.0f, 5.0f, 2.0f,   [this] { return depthMs.load(); },  [this] (float v) { depthMs.store (v); } },
            { "Feedbk", 0.0f, 0.95f, 0.5f,  [this] { return feedback.load(); }, [this] (float v) { feedback.store (v); } },
            { "Mix",    0.0f, 1.0f, 0.5f,   [this] { return mix.load(); },      [this] (float v) { mix.store (v); } }
        };
    }

private:
    double sr { 44100.0 }, bpm { 120.0 };
    int    bufLen { 0 }, writePos { 0 };
    float  phase { 0.0f };
    std::array<std::vector<float>, 2> buffers;
    std::atomic<float> rateHz { 0.5f }, depthMs { 2.0f }, feedback { 0.5f }, mix { 0.5f }, syncBeats { 0.0f };
};

// ---------------------------------------------------------------------------
// Phaser — a cascade of LFO-swept allpass stages summed with the dry signal,
// producing sweeping notches (see AllpassPhaser.h for the pure DSP primitives).
// ---------------------------------------------------------------------------
class PhaserFx : public Effect
{
public:
    void prepare (double sampleRate, int, int) override { sr = sampleRate; reset(); }

    void reset() override
    {
        for (auto& ch : z) ch.fill (0.0f);
        fbState.fill (0.0f);
        phase = 0.0f;
    }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load()) return;
        const float rate  = effectSyncedRate (bpm, syncBeats.load(), rateHz.load());   // tempo-sync when Sync bt > 0
        const float depth = juce::jlimit (0.0f, 1.0f, depthAmt.load());
        const float fb    = juce::jlimit (0.0f, 0.95f, feedback.load());
        const float mx    = juce::jlimit (0.0f, 1.0f, mix.load());
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float inc   = twoPi * rate / (float) sr;
        const float fmin  = 200.0f;
        const float fmax  = fmin * std::pow (2.0f, 1.0f + 4.0f * depth);   // up to ~5 octaves of sweep

        const int nch = juce::jmin (2, b.getNumChannels());
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float lfo = 0.5f + 0.5f * std::sin (phase);             // 0..1
            const float fc  = fmin * std::pow (fmax / fmin, lfo);         // log sweep
            const float a   = phaserCoeff (fc, sr);
            for (int c = 0; c < nch; ++c)
            {
                auto* d = b.getWritePointer (c);
                const float in = d[i];
                float x = in + fbState[(size_t) c] * fb;
                for (int s = 0; s < kStages; ++s)
                    x = allpassStage (x, a, z[(size_t) c][(size_t) s]);
                fbState[(size_t) c] = x;
                d[i] = in * (1.0f - mx) + x * mx;
            }
            phase += inc; if (phase >= twoPi) phase -= twoPi;
        }
    }

    void setTempo (double b) override { bpm = juce::jmax (1.0, b); }

    juce::String name() const override { return "Phaser"; }

    std::vector<EffectParam> parameters() override
    {
        return {
            { "Rate",   0.05f, 8.0f, 0.4f,  [this] { return rateHz.load(); },   [this] (float v) { rateHz.store (v); } },
            { "Sync bt", 0.0f, 4.0f, 0.0f,  [this] { return syncBeats.load(); }, [this] (float v) { syncBeats.store (v); } },
            { "Depth",  0.0f, 1.0f, 0.6f,   [this] { return depthAmt.load(); }, [this] (float v) { depthAmt.store (v); } },
            { "Feedbk", 0.0f, 0.95f, 0.5f,  [this] { return feedback.load(); }, [this] (float v) { feedback.store (v); } },
            { "Mix",    0.0f, 1.0f, 0.5f,   [this] { return mix.load(); },      [this] (float v) { mix.store (v); } }
        };
    }

private:
    static constexpr int kStages = 6;
    double sr { 44100.0 }, bpm { 120.0 };
    float  phase { 0.0f };
    std::array<std::array<float, kStages>, 2> z {};
    std::array<float, 2> fbState {};
    std::atomic<float> rateHz { 0.4f }, depthAmt { 0.6f }, feedback { 0.5f }, mix { 0.5f }, syncBeats { 0.0f };
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Oscilloscope — a NON-MUTATING analyzer insert. It passes audio through
// unchanged but captures a decimated mono (left-channel) waveform of the recent
// signal into a lock-free ring, retrievable over the control API and drawn live
// in the device panel. The audio thread only stores into std::atomic<float>s
// (no lock, no allocation — principle 4); the reader copies the ring oldest->newest.
// ---------------------------------------------------------------------------
class ScopeFx : public Effect
{
public:
    static constexpr int kPoints = 256;                 // waveform points in the snapshot

    void prepare (double, int, int) override {}
    void reset() override { for (auto& s : ring) s.store (0.0f); widx.store (0); }

    void process (juce::AudioBuffer<float>& b) override
    {
        // Non-mutating: never writes to the buffer. Capture the left channel, decimated
        // so the ~256-point window spans a musically useful ~40 ms rather than one block.
        if (b.getNumChannels() < 1) return;
        const int n = b.getNumSamples();
        const auto* L = b.getReadPointer (0);
        int w = widx.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            if (++decimCount >= kDecim)
            {
                decimCount = 0;
                ring[(size_t) w].store (L[i], std::memory_order_relaxed);
                w = (w + 1) % kPoints;
            }
        widx.store (w, std::memory_order_relaxed);
    }

    juce::String name() const override { return "Scope"; }
    std::vector<EffectParam> parameters() override { return {}; }   // no knobs — the display is the UI

    int analyzerSnapshot (float* out, int maxN) const override
    {
        const int count = juce::jmin (maxN, kPoints);
        const int w = widx.load (std::memory_order_relaxed);        // oldest sample sits at w
        for (int i = 0; i < count; ++i)
            out[i] = ring[(size_t) ((w + i) % kPoints)].load (std::memory_order_relaxed);
        return count;
    }

private:
    static constexpr int kDecim = 8;                    // keep 1 of every 8 samples
    std::array<std::atomic<float>, kPoints> ring {};
    std::atomic<int> widx { 0 };
    int decimCount { 0 };                               // audio-thread only
};

// ---------------------------------------------------------------------------
// Spectrum analyzer (RTA) — a NON-MUTATING analyzer insert. Passes audio through
// unchanged but runs the mono (left) signal through a bank of octave bandpass
// filters (10 bands, 31 Hz..16 kHz), each followed by an envelope follower, and
// exposes the per-band levels over the control API — a classic real-time analyzer
// (no FFT). The audio thread writes only std::atomic<float> band levels (no lock,
// no allocation; principle 4).
// ---------------------------------------------------------------------------
class SpectrumFx : public Effect
{
public:
    static constexpr int kBands = 10;                   // octave bands 31.25 Hz .. 16 kHz

    void prepare (double sampleRate, int, int) override
    {
        sr = (float) sampleRate;
        const float k = 1.0f / q;                       // SVF resonance term (shared)
        for (int band = 0; band < kBands; ++band)
        {
            float fc = juce::jlimit (20.0f, sr * 0.49f, centreHz (band));
            const float g = std::tan (juce::MathConstants<float>::pi * fc / sr);
            a1[band] = 1.0f / (1.0f + g * (g + k));
            a2[band] = g * a1[band];
            a3[band] = g * a2[band];
        }
        atCoef = std::exp (-1.0f / (0.005f * sr));       // 5 ms attack
        rlCoef = std::exp (-1.0f / (0.150f * sr));       // 150 ms release
        reset();
    }

    void reset() override
    {
        for (auto& s : ic1) s = 0.0f;
        for (auto& s : ic2) s = 0.0f;
        for (auto& e : env) e = 0.0f;
        for (auto& l : level) l.store (0.0f);
    }

    void process (juce::AudioBuffer<float>& b) override
    {
        if (b.getNumChannels() < 1) return;              // non-mutating: never writes the buffer
        const int n = b.getNumSamples();
        const auto* x = b.getReadPointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float in = x[i];
            for (int band = 0; band < kBands; ++band)
            {
                const float v3 = in - ic2[(size_t) band];             // TPT SVF; v1 = bandpass output
                const float v1 = a1[band] * ic1[(size_t) band] + a2[band] * v3;
                const float v2 = ic2[(size_t) band] + a2[band] * ic1[(size_t) band] + a3[band] * v3;
                ic1[(size_t) band] = 2.0f * v1 - ic1[(size_t) band];
                ic2[(size_t) band] = 2.0f * v2 - ic2[(size_t) band];
                const float a = std::abs (v1 * q);                    // compensate the bandpass gain (~1/q)
                float& e = env[(size_t) band];
                e = a > e ? atCoef * (e - a) + a : rlCoef * (e - a) + a;
            }
        }
        for (int band = 0; band < kBands; ++band)
            level[(size_t) band].store (env[(size_t) band], std::memory_order_relaxed);
    }

    juce::String name() const override { return "Spectrum"; }
    std::vector<EffectParam> parameters() override { return {}; }    // no knobs — the display is the UI

    int analyzerSnapshot (float* out, int maxN) const override
    {
        const int count = juce::jmin (maxN, kBands);
        for (int i = 0; i < count; ++i)
            out[i] = level[(size_t) i].load (std::memory_order_relaxed);
        return count;
    }

private:
    static float centreHz (int band) { return 31.25f * std::pow (2.0f, (float) band); }

    static constexpr float q = 2.9f;                    // per-band bandpass Q (separates adjacent tones)
    float sr { 44100.0f }, atCoef { 0.0f }, rlCoef { 0.0f };
    std::array<float, kBands> a1 {}, a2 {}, a3 {};      // per-band SVF coeffs (prepare-time)
    std::array<float, kBands> ic1 {}, ic2 {}, env {};   // audio-thread state
    std::array<std::atomic<float>, kBands> level {};    // published band levels
};

namespace EffectFactory
{
    inline juce::StringArray types() { return { "Gain", "Filter", "Delay", "Reverb", "Limiter", "Bitcrusher", "Compressor", "EQ", "Waveshaper", "Stereo Widener", "Tremolo", "Chorus", "Flanger", "Phaser", "Auto-pan", "Noise Gate", "Auto-wah", "Ring Mod", "Scope", "Spectrum" }; }

    inline std::unique_ptr<Effect> create (const juce::String& type)
    {
        if (type == "Gain")       return std::make_unique<GainFx>();
        if (type == "Filter")     return std::make_unique<FilterFx>();
        if (type == "Delay")      return std::make_unique<DelayFx>();
        if (type == "Reverb")     return std::make_unique<ReverbFx>();
        if (type == "Limiter")    return std::make_unique<LimiterFx>();
        if (type == "Bitcrusher") return std::make_unique<BitcrusherFx>();
        if (type == "Compressor") return std::make_unique<CompressorFx>();
        if (type == "EQ")         return std::make_unique<EqFx>();
        if (type == "Waveshaper") return std::make_unique<WaveshaperFx>();
        if (type == "Stereo Widener") return std::make_unique<StereoWidenerFx>();
        if (type == "Tremolo")    return std::make_unique<TremoloFx>();
        if (type == "Chorus")     return std::make_unique<ChorusFx>();
        if (type == "Flanger")    return std::make_unique<FlangerFx>();
        if (type == "Phaser")     return std::make_unique<PhaserFx>();
        if (type == "Auto-pan")   return std::make_unique<AutoPanFx>();
        if (type == "Noise Gate") return std::make_unique<NoiseGateFx>();
        if (type == "Auto-wah")   return std::make_unique<AutoWahFx>();
        if (type == "Ring Mod")   return std::make_unique<RingModFx>();
        if (type == "Scope")      return std::make_unique<ScopeFx>();
        if (type == "Spectrum")   return std::make_unique<SpectrumFx>();
        return nullptr;
    }
}
