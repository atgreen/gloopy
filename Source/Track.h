// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <atomic>
#include "Generator.h"
#include "Clip.h"
#include "LiveArp.h"
#include "SessionModel.h"

enum class TrackType { Instrument, Audio, MidiOut };

/** A track in the linear arrangement: a sound source (for instrument tracks)
    plus a row of clips on the timeline, and its mix settings. Unifies the old
    Channel + Pattern + playlist-lane into one thing. */
struct Track
{
    Track (juce::String nameToUse, std::unique_ptr<Generator> gen,
           int pitch, juce::Colour col, TrackType t = TrackType::Instrument)
        : name (std::move (nameToUse)), type (t),
          generator (std::move (gen)), defaultPitch (pitch), colour (col)
    {}

    juce::String               name;
    TrackType                  type;
    std::unique_ptr<Generator> generator;     // instrument tracks
    int                        defaultPitch;   // pitch used by step-style editing
    juce::Colour               colour;

    int id { -1 };                             // stable id for the control API
    juce::MidiMessageCollector liveMidi;       // real-time notes injected via OSC

    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan    { 0.0f };
    std::atomic<bool>  mute   { false };
    std::atomic<bool>  solo   { false };
    std::atomic<bool>  polarity { false };   // phase invert: negate this track's contribution
    std::atomic<int>   mixerTrack { 0 };

    // Audio-recording state (project state, not global prefs).
    std::atomic<bool> recordArmed   { false };
    std::atomic<int>  recordInput   { 0 };   // first hardware input channel
    std::atomic<int>  recordChannels{ 2 };   // 1 = mono, 2 = stereo
    std::atomic<bool> recordMonitor { false };

    // Live arpeggiator (non-destructive): when enabled, each clip's chords are played as
    // a stepped pattern (Clip::arpNotes, recomputed off-thread). Edited under the engine lock.
    struct ArpSpec
    {
        bool   enabled { false };
        double rate    { 0.25 };   // step length in beats
        int    octaves { 1 };
        float  gate    { 0.5f };   // fraction of the step the note sounds
        int    mode    { 0 };      // 0 up, 1 down, 2 up-down, 3 random
        float  swing   { 0.0f };   // 0 = straight; delays every other step
        bool   hold    { false };  // latch the last chord across rests / fill the clip
        float  probability { 1.0f };  // chance each generated step actually fires (deterministic gate)
    };
    ArpSpec arp;

    // Real-time arp engine for live keyboard/OSC input: when arp.enabled, live notes are run
    // through this instead of played straight, so a held chord arpeggiates live. Audio-thread
    // state; reset() on arp-off / panic. (The clip arp above is the non-destructive playback one.)
    LiveArp liveArp;

    std::vector<Clip> clips;   // arrangement clips on the timeline; guarded by the engine lock

    // Session view (clip-launch grid): one launchable slot per global scene (null = empty).
    // Kept the same length as MainComponent's scene list (see SessionModel.h). Guarded by the
    // engine lock; the launch engine (later slice) reads it on the audio thread.
    SessionSlots sessionSlots;
};
