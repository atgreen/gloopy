// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>

/** A sound source for one channel. Both the sampler and the synth implement
    this, so the audio engine can drive every channel uniformly with note
    events (a juce::MidiBuffer) and mix the result. */
class Generator
{
public:
    /** One addressable sound within a generator (a drum pad, a kit voice). A
        multi-voice instrument returns one per voice; a single-voice instrument
        returns none, and the step/piano editors treat it as one melodic lane. */
    struct Voice
    {
        juce::String name;
        int          note   { 60 };            // MIDI note that triggers this voice
        juce::Colour colour { 0xff9aa0a6 };
    };

    virtual ~Generator() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Render @p numSamples into @p buffer starting at @p startSample, applying
        the note events in @p midi (timestamps relative to @p startSample). */
    virtual void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                         int startSample, int numSamples) = 0;

    virtual void allNotesOff() = 0;

    /** Hint that we're doing a faster-than-real-time offline bounce. Disk-streaming
        generators (sfizz) switch to synchronous loading so the render doesn't
        outrun the streaming thread and drop to silence. No-op for the rest. */
    virtual void setFreewheeling (bool /*offline*/) {}

    virtual juce::String typeName() const = 0;

    /** The addressable voices of a multi-voice instrument (drum-kit pads), in row
        order. Empty for single-voice instruments — the caller then treats the whole
        generator as one lane at the track's default pitch. */
    virtual std::vector<Voice> voices() const { return {}; }

    /** Non-null for hosted plugins, so the UI can open their editor. */
    virtual juce::AudioProcessor* getPluginInstance() { return nullptr; }

    /** Message-thread cache of a display name for hosted instruments — e.g. the current Surge XT
        patch. Empty = the track uses its default name/type label. Read in paint (cheap);
        refreshed sparingly by the host via refreshUiPatchName() (getStateInformation is heavy). */
    juce::String uiPatchName;

    /** Recompute uiPatchName from the live plugin state. Message thread only; may be costly. */
    virtual void refreshUiPatchName() {}
};
