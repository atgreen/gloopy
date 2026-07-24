// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "Note.h"

/** Thread-safe store of notes plus the logic that turns them into MIDI events
    for a given block of the loop. The GUI thread mutates the note list under a
    lock; the audio thread reads it with a *try*-lock so it never blocks (if it
    can't grab the lock it simply emits no new events for that block — notes
    already sounding keep playing). */
class Sequencer
{
public:
    // ---- GUI-thread API (blocking lock) --------------------------------
    void replaceAll (std::vector<Note> newNotes)
    {
        const juce::ScopedLock sl (lock);
        notes = std::move (newNotes);
    }

    void clear()
    {
        const juce::ScopedLock sl (lock);
        notes.clear();
    }

    std::vector<Note> snapshot() const
    {
        const juce::ScopedLock sl (lock);
        return notes;
    }

    // ---- Audio-thread API (non-blocking try-lock) ----------------------
    /** Appends note-on/off events that fall within [loopPosStart, loopPosStart+chunk)
        to @p midi. Event timestamps are written as tsOffset + (eventSample - loopPosStart),
        i.e. relative to the same coordinate system the caller renders with. */
    bool collect (juce::MidiBuffer& midi, juce::int64 loopPosStart, int chunk,
                  int tsOffset, double samplesPerBeat) const
    {
        const juce::ScopedTryLock stl (lock);
        if (! stl.isLocked())
            return false;

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

        return true;
    }

private:
    std::vector<Note>            notes;
    mutable juce::CriticalSection lock;
};
