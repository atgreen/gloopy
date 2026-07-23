#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "Note.h"

/** Appends note-on/off events for the notes that fall within
    [loopPosStart, loopPosStart + chunk) to @p midi, timestamped as
    tsOffset + (eventSample - loopPosStart). Shared by every channel's playback. */
inline void collectNotes (const std::vector<Note>& notes, juce::MidiBuffer& midi,
                          juce::int64 loopPosStart, int chunk, int tsOffset,
                          double samplesPerBeat)
{
    const juce::int64 loopPosEnd = loopPosStart + chunk;

    for (const auto& n : notes)
    {
        const juce::int64 on  = (juce::int64) std::llround (n.startBeat * samplesPerBeat);
        const juce::int64 off = (juce::int64) std::llround ((n.startBeat + n.lengthBeats) * samplesPerBeat);

        if (on >= loopPosStart && on < loopPosEnd)
            midi.addEvent (juce::MidiMessage::noteOn (1, n.pitch, n.velocity),
                           tsOffset + (int) (on - loopPosStart));

        if (off >= loopPosStart && off < loopPosEnd)
            midi.addEvent (juce::MidiMessage::noteOff (1, n.pitch),
                           tsOffset + (int) (off - loopPosStart));
    }
}
