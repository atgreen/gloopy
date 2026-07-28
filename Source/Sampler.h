// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

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

    // Playback window: start/end are fractions [0,1] of the sample length; reverse plays
    // the window back-to-front. New note-ons pick these up; already-ringing voices keep
    // their direction but honour the (possibly changed) bounds for deactivation.
    void  setPlaybackWindow (float s, float e, bool rev)
    {
        startFrac = juce::jlimit (0.0f, 1.0f, s);
        endFrac   = juce::jlimit (0.0f, 1.0f, e);
        reverse   = rev;
    }
    float getStartFrac() const { return startFrac; }
    float getEndFrac()   const { return endFrac; }
    bool  getReverse()   const { return reverse; }

    // Per-voice amplitude fades, in seconds (0 = off). In loop mode fadeOut is the
    // release time applied on note-off (see render); otherwise it fades the window end.
    void  setFades (float in, float out) { fadeIn = juce::jmax (0.0f, in); fadeOut = juce::jmax (0.0f, out); }
    float getFadeIn()  const { return fadeIn; }
    float getFadeOut() const { return fadeOut; }

    // Loop mode: the playback window repeats (sustained) until a note-off releases it,
    // instead of playing once. Note-offs still ring out for a one-shot (loop off).
    void setLoop (bool shouldLoop) { loop = shouldLoop; }
    bool getLoop() const { return loop; }

    // Loop crossfade (seconds, 0 = off): as playback approaches the loop wrap, blend the
    // loop-end content with the material that continues after the wrap, so the seam has no
    // discontinuity click. Only active in loop mode. Needs headroom before the loop start
    // (a startFrac > 0) for the smoothest blend; with the window at the very start it clamps.
    void  setLoopXfade (float seconds) { loopXfade = juce::jmax (0.0f, seconds); }
    float getLoopXfade() const { return loopXfade; }

    // Mono / choke: a new note-on cuts every currently-ringing voice, so overlapping hits
    // don't stack (classic hi-hat choke / mono 808). Off = polyphonic (voices ring out).
    void setMono (bool m) { mono = m; }
    bool getMono() const { return mono; }

    // Accessors for project serialization.
    const juce::AudioBuffer<float>& getSampleBuffer() const { return sample; }
    double getSourceRate() const { return sourceRate; }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override
    {
        if (sample.getNumSamples() == 0)
            return;

        const int lo = windowStartFrame(), hi = windowEndFrame();

        auto midiIt = midi.cbegin();
        const auto midiEnd = midi.cend();

        for (int i = 0; i < numSamples; ++i)
        {
            const int framePos = startSample + i;

            // Trigger any voices whose event lands on this frame. In loop mode a note-off
            // starts the release (fadeOut, or an immediate stop when fadeOut is 0); a
            // one-shot ignores note-offs (drums ring out), unchanged.
            while (midiIt != midiEnd && (*midiIt).samplePosition <= framePos)
            {
                const auto msg = (*midiIt).getMessage();
                if (msg.isNoteOn() && msg.getFloatVelocity() > 0.0f)
                    startVoice (msg.getNoteNumber(), msg.getFloatVelocity());
                else if (loop && (msg.isNoteOff() || msg.isNoteOn()))     // note-on vel 0 == note-off
                    for (auto& v : voices)
                        if (v.active && v.note == msg.getNoteNumber() && ! v.releasing)
                            { v.releasing = true; v.relAge = 0.0; }
                ++midiIt;
            }

            const double span = (double) (hi - 1 - lo);
            float mono = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active)
                    continue;

                const float sv = (loop && loopXfade > 0.0f)
                                    ? readLoopXfade (v.pos, lo, hi, span, v.rate >= 0.0)
                                    : readInterpolated (v.pos);
                mono += sv * v.gain * envelopeGain (v);
                v.pos += v.rate;                          // rate is signed (negative = reverse)
                v.age += 1.0;                             // output samples since trigger

                if (loop)
                {
                    // Wrap at the window edges and keep playing until note-off releases it.
                    if (v.rate >= 0.0) { if (v.pos >= (double) (hi - 1)) v.pos -= span; }
                    else               { if (v.pos <= (double) lo)       v.pos += span; }
                    if (v.releasing)
                    {
                        v.relAge += 1.0;
                        if (fadeOut <= 0.0f || v.relAge >= (double) fadeOut * deviceRate)
                            v.active = false;
                    }
                }
                else if (v.rate >= 0.0 ? (v.pos >= (double) hi) : (v.pos <= (double) lo))
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
        bool   active    { false };
        double pos       { 0.0 };
        double rate      { 1.0 };
        float  gain      { 1.0f };
        double age       { 0.0 };    // output samples since the note-on
        double life      { 0.0 };    // total output samples the voice will play (window / |rate|)
        int    note      { -1 };     // MIDI note this voice plays (for loop-mode note-off matching)
        bool   releasing { false };  // loop mode: note-off received, releasing
        double relAge    { 0.0 };    // output samples since the note-off
    };

    // Amplitude envelope for a voice: a linear fade-in from the note-on and a linear
    // fade-out approaching the end of its playback window, in seconds. Declicks samples
    // trimmed mid-waveform by the playback window. Returns 1.0 when both fades are off.
    float envelopeGain (const Voice& v) const
    {
        float g = 1.0f;
        if (fadeIn > 0.0f)
        {
            const double fadeSamples = (double) fadeIn * deviceRate;
            if (v.age < fadeSamples) g *= (float) (v.age / fadeSamples);
        }
        if (loop)
        {
            // Release fade on note-off (fadeOut = release time). No window-end fade while looping.
            if (v.releasing && fadeOut > 0.0f)
            {
                const double fadeSamples = (double) fadeOut * deviceRate;
                g *= (float) juce::jmax (0.0, 1.0 - v.relAge / fadeSamples);
            }
        }
        else if (fadeOut > 0.0f)
        {
            const double fadeSamples = (double) fadeOut * deviceRate;
            const double remaining   = v.life - v.age;
            if (remaining < fadeSamples) g *= (float) juce::jmax (0.0, remaining / fadeSamples);
        }
        return g;
    }

    // The playback window in source frames, kept in-bounds so readInterpolated (which
    // reads pos and pos+1) never runs off the buffer: [0, len-2] for the low edge,
    // [lo+1, len-1] for the high edge.
    int windowStartFrame() const
    {
        const int len = sample.getNumSamples();
        return juce::jlimit (0, juce::jmax (0, len - 2), (int) ((double) startFrac * (len - 1)));
    }
    int windowEndFrame() const
    {
        const int len = sample.getNumSamples();
        return juce::jlimit (windowStartFrame() + 1, juce::jmax (1, len - 1), (int) ((double) endFrac * (len - 1)));
    }

    void startVoice (int noteNumber, float velocity)
    {
        if (mono)                                    // choke: cut every ringing voice first
            for (auto& v : voices) v.active = false;
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active) { slot = &v; break; }
        if (slot == nullptr)
            slot = &voices[0];   // steal the first voice

        const double pitchRatio = std::pow (2.0, (noteNumber - rootNote) / 12.0);
        const double baseRate   = (sourceRate / deviceRate) * pitchRatio;
        const int lo = windowStartFrame(), hi = windowEndFrame();
        slot->active = true;
        slot->rate   = reverse ? -baseRate : baseRate;
        slot->pos    = reverse ? (double) (hi - 1) : (double) lo;
        slot->gain   = velocity;
        slot->age    = 0.0;
        slot->life   = baseRate > 0.0 ? (double) (hi - 1 - lo) / baseRate : 0.0;   // output samples to traverse the window
        slot->note   = noteNumber;
        slot->releasing = false;
        slot->relAge = 0.0;
    }

    float readInterpolated (double pos) const
    {
        const int   i0   = (int) pos;
        const int   i1   = i0 + 1;
        const float frac = (float) (pos - i0);
        const auto* d = sample.getReadPointer (0);
        return d[i0] * (1.0f - frac) + d[i1] * frac;
    }

    // Crossfaded loop read: within `loopXfade` of the wrap point, blend the current content
    // with the material one loop-span away (which is what plays after the wrap) so the seam is
    // continuous. Forward wraps at hi-1 (blend toward pos-span, the pre-start material);
    // reverse wraps at lo (blend toward pos+span). The other-read is clamped in-buffer.
    float readLoopXfade (double pos, int lo, int hi, double span, bool forward) const
    {
        const float s = readInterpolated (pos);
        double xf = (double) loopXfade * sourceRate;                  // xfade length in source frames
        if (xf < 1.0 || span < 2.0) return s;
        xf = juce::jmin (xf, span - 1.0);                             // can't exceed the loop
        const double dist = forward ? ((double) (hi - 1) - pos) : (pos - (double) lo);   // frames until wrap
        if (dist < 0.0 || dist >= xf) return s;
        const float  t = (float) (1.0 - dist / xf);                  // 0 at region entry -> 1 at the wrap
        double other = forward ? (pos - span) : (pos + span);
        other = juce::jlimit (0.0, (double) juce::jmax (0, sample.getNumSamples() - 2), other);
        return (1.0f - t) * s + t * readInterpolated (other);
    }

    juce::AudioBuffer<float> sample;
    double      sourceRate { 44100.0 };
    double      deviceRate { 44100.0 };
    int         rootNote   { 60 };
    float       startFrac  { 0.0f };   // playback window start (fraction of length)
    float       endFrac    { 1.0f };   // playback window end
    bool        reverse    { false };  // play the window back-to-front
    float       fadeIn     { 0.0f };   // per-voice fade-in (seconds)
    float       fadeOut    { 0.0f };   // per-voice fade-out / release (seconds)
    bool        loop       { false };  // window repeats until note-off (vs one-shot)
    float       loopXfade  { 0.0f };   // loop-seam crossfade (seconds, 0 = off)
    bool        mono       { false };  // choke: a new note-on cuts all ringing voices
    juce::String sampleName;

    static constexpr int kNumVoices = 8;
    std::array<Voice, kNumVoices> voices;
};
