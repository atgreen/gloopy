#pragma once

#include <JuceHeader.h>
#include <vector>
#include "Note.h"

/** A clip placed on a track's timeline. In this linear-arranger model a clip
    OWNS its content (its own notes); to repeat, you loop the clip or copy it.

    A MIDI clip edits/loops a content window of @c contentLenBeats. When @c looped
    and the clip is longer than the content, the content repeats to fill it. */
struct Clip
{
    juce::String name;
    double startBeat      { 0.0 };
    double lengthBeats    { 4.0 };

    // MIDI content (owned).
    std::vector<Note> notes;
    double contentLenBeats { 4.0 };   // the editable / looped unit
    bool   looped          { true };

    double endBeat() const noexcept { return startBeat + lengthBeats; }
};
