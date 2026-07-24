// SPDX-FileCopyrightText: 2026 Anthony Green <anthony@atgreen.org>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

/** A single MIDI note in the sequencer timeline.
    Time is expressed in beats (quarter notes) relative to the loop start, so it
    stays tempo-independent until the audio thread converts it to samples. */
struct Note
{
    int    pitch       { 60 };    // MIDI note number (0-127)
    double startBeat   { 0.0 };   // start position in beats from loop start
    double lengthBeats { 1.0 };   // duration in beats
    float  velocity    { 0.8f };  // 0..1
};
