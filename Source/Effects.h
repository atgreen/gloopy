#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>
#include "Effect.h"

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

    void process (juce::AudioBuffer<float>& b) override
    {
        if (bypassed.load() || bufLen <= 4) return;

        const float base = juce::jlimit (1.0f, (float) (bufLen - 4), (timeMs.load() / 1000.0f) * (float) sr);
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
            { "Feedbk", 0.0f, 0.95f, 0.35f,
              [this] { return feedback.load(); }, [this] (float v) { feedback.store (v); } },
            { "Mix",    0.0f, 1.0f, 0.3f,
              [this] { return mix.load(); }, [this] (float v) { mix.store (v); } }
        };
    }

private:
    double sr { 44100.0 };
    int    bufLen { 0 }, writePos { 0 };
    std::array<std::vector<float>, 2> buffers;
    std::atomic<float> timeMs { 300.0f }, feedback { 0.35f }, mix { 0.3f };
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

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
namespace EffectFactory
{
    inline juce::StringArray types() { return { "Gain", "Filter", "Delay", "Reverb" }; }

    inline std::unique_ptr<Effect> create (const juce::String& type)
    {
        if (type == "Gain")   return std::make_unique<GainFx>();
        if (type == "Filter") return std::make_unique<FilterFx>();
        if (type == "Delay")  return std::make_unique<DelayFx>();
        if (type == "Reverb") return std::make_unique<ReverbFx>();
        return nullptr;
    }
}
