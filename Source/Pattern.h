// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "Note.h"

/** A pattern holds, per channel, a list of notes. The step sequencer is a
    constrained editor over these lists: toggling a step adds/removes a note at
    a fixed grid position and the channel's default pitch. The piano roll (M2)
    will edit the same lists freely.

    Not internally locked — access is guarded by the audio engine's lock. */
class Pattern
{
public:
    static constexpr int stepsPerBeat = 4;   // 16 steps == 4 beats == 1 bar

    explicit Pattern (int steps = 16) : lengthSteps (steps) {}

    juce::String name;
    juce::Colour colour { juce::Colour (0xff4a90d9) };

    void setChannelCount (int n)          { lanes.resize ((size_t) juce::jmax (0, n)); }
    int  getChannelCount() const noexcept { return (int) lanes.size(); }

    int    getLengthSteps() const noexcept  { return lengthSteps; }
    int    getLengthBeats() const noexcept  { return lengthSteps / stepsPerBeat; }
    double stepDurationBeats() const noexcept { return 1.0 / (double) stepsPerBeat; }

    int stepOfBeat (double beat) const noexcept
    {
        return (int) std::llround (beat / stepDurationBeats());
    }

    /** Toggle a step on/off for a channel. If turning on, the note uses the
        given pitch and velocity. */
    void toggleStep (int channel, int step, int pitch, float velocity)
    {
        if (! juce::isPositiveAndBelow (channel, getChannelCount()))
            return;

        auto& lane = lanes[(size_t) channel];
        for (size_t i = 0; i < lane.size(); ++i)
        {
            if (stepOfBeat (lane[i].startBeat) == step)
            {
                lane.erase (lane.begin() + (long) i);
                return;
            }
        }
        lane.push_back ({ pitch, step * stepDurationBeats(), stepDurationBeats(), velocity });
    }

    bool isStepOn (int channel, int step) const
    {
        if (! juce::isPositiveAndBelow (channel, getChannelCount()))
            return false;

        for (const auto& n : lanes[(size_t) channel])
            if (stepOfBeat (n.startBeat) == step)
                return true;
        return false;
    }

    void clearChannel (int channel)
    {
        if (juce::isPositiveAndBelow (channel, getChannelCount()))
            lanes[(size_t) channel].clear();
    }

    /** Replace a channel's whole note list (used by the piano roll). */
    void setLane (int channel, std::vector<Note> newNotes)
    {
        if (juce::isPositiveAndBelow (channel, getChannelCount()))
            lanes[(size_t) channel] = std::move (newNotes);
    }

    const std::vector<Note>& lane (int channel) const { return lanes[(size_t) channel]; }

private:
    int lengthSteps;
    std::vector<std::vector<Note>> lanes;
};
