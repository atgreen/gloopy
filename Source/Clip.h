// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "Note.h"

enum class ClipType { Midi, Audio };

/** A clip placed on a track's timeline. In this linear-arranger model a clip
    OWNS its content. A MIDI clip holds notes (looping a content window); an
    audio clip holds sample data played at its natural speed. */
struct Clip
{
    ClipType     type { ClipType::Midi };
    juce::String name;
    double startBeat   { 0.0 };
    double lengthBeats { 4.0 };

    // MIDI content.
    std::vector<Note> notes;
    double contentLenBeats { 4.0 };
    bool   looped          { true };

    // Audio content (shared so copies are cheap).
    std::shared_ptr<juce::AudioBuffer<float>> audio;
    double audioSourceRate { 44100.0 };
    float  audioGain       { 1.0f };
    std::shared_ptr<std::vector<float>> peaks;   // display overview (max-abs per bucket)
    // When non-empty, the audio came from a file (a recorded take or imported
    // reference) — serialised as a path, not embedded. `audio` still holds the
    // decoded buffer for playback; this just records where it came from.
    juce::String audioFile;
    juce::String takeId;

    bool   muted { false };      // take-lane: inactive alternate takes are muted
    double endBeat() const noexcept { return startBeat + lengthBeats; }
    bool   isAudio() const noexcept { return type == ClipType::Audio; }
};

/** Build a fixed-resolution max-abs overview of a buffer for waveform drawing. */
inline std::vector<float> buildPeaks (const juce::AudioBuffer<float>& buf, int buckets = 1024)
{
    std::vector<float> peaks ((size_t) buckets, 0.0f);
    const int frames = buf.getNumSamples();
    if (frames <= 0)
        return peaks;

    for (int b = 0; b < buckets; ++b)
    {
        const int s0 = (int) ((juce::int64) b * frames / buckets);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (b + 1) * frames / buckets));
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = s0; i < s1 && i < frames; ++i)
                peak = juce::jmax (peak, std::abs (d[i]));
        }
        peaks[(size_t) b] = peak;
    }
    return peaks;
}
