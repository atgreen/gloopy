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
    gloopy::time::BeatRatio startBeat   {};      // timeline position (beats), exact
    gloopy::time::BeatRatio lengthBeats { 4, 1 }; // length on the timeline (beats), exact

    // MIDI content.
    std::vector<Note> notes;
    std::vector<Note> arpNotes;    // transient: live-arp expansion of `notes`, played when the track's arp is on
    gloopy::time::BeatRatio contentLenBeats { 4, 1 };   // loop-content window (beats), exact
    bool   looped          { true };
    int    transpose       { 0 };    // non-destructive playback pitch offset (semitones)
    float  velocityScale   { 1.0f };  // non-destructive playback velocity multiplier (0..2)

    // Linked / pooled clips: when non-empty, every clip sharing this id shares one pattern —
    // editing the notes of any one propagates to all (MainComponent::syncLinkedClips). Placement
    // (start/length/loop/transpose/velocity/colour) stays per-instance. Empty = independent.
    juce::String linkId;
    bool   isLinked() const noexcept { return linkId.isNotEmpty(); }

    // Audio content (shared so copies are cheap).
    std::shared_ptr<juce::AudioBuffer<float>> audio;
    double audioSourceRate { 44100.0 };
    float  audioGain       { 1.0f };
    double fadeInBeats     { 0.0 };   // fade-in / fade-out over the clip edges (audio)
    double fadeOutBeats    { 0.0 };
    int    fadeShape       { 0 };     // 0 linear, 1 equal-power, 2 exponential (see FadeShape.h)
    std::shared_ptr<std::vector<float>> peaks;   // display overview (max-abs per bucket)
    // When non-empty, the audio came from a file (a recorded take or imported
    // reference) — serialised as a path, not embedded. `audio` still holds the
    // decoded buffer for playback; this just records where it came from.
    juce::String audioFile;
    juce::String takeId;

    // Script clip: when scriptSource is non-empty, this clip's `notes` are the CACHED
    // output of a language kernel (see the gloopy.v1.Kernel service). scriptSource is the
    // source-file path relative to the composition; the notes are the materialised
    // "lockfile" so the clip plays without a runtime — only regenerating needs the kernel.
    juce::String scriptSource;         // source-file path (project-relative) — the file-based generator
    juce::String scriptGenerator;      // OR a named generator: "pkg.mod:fn" (py) / "pkg:sym" (lisp)
    juce::String scriptSystem;         // ASDF system to load for a named Lisp generator (optional)
    juce::String scriptLang;           // kernel/language id, e.g. "common-lisp", "python"
    juce::int64  scriptSeed { 0 };     // deterministic RNG seed passed to the kernel
    bool   scriptLive { false };       // "live": auto-regenerate ~1 bar before playback (kernel image)
    // A script clip's notes come from code — either a source file (scriptSource) or a live
    // kernel generator identified by language (scriptLang; e.g. a notebook-defined Python
    // generator with no file on disk).
    bool   isScript() const noexcept { return scriptSource.isNotEmpty() || scriptGenerator.isNotEmpty() || scriptLang.isNotEmpty(); }

    bool   muted { false };      // take-lane: inactive alternate takes are muted
    juce::Colour colour;         // per-clip colour override; ARGB 0 (default) = inherit the track colour
    double endBeat() const noexcept { return (startBeat + lengthBeats).toBeats(); }
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
