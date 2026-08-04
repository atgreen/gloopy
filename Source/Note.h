// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "Rational.h"

/** A single MIDI note in the sequencer timeline.
    Time is expressed in beats (quarter notes) relative to the loop start, so it
    stays tempo-independent until the audio thread converts it to samples. Positions
    are exact rationals (BeatRatio) so quantize/loop/equality don't drift; call
    .toBeats() at the DSP/UI boundary where a double is needed. */
struct Note
{
    int    pitch       { 60 };            // MIDI note number (0-127)
    gloopy::time::BeatRatio startBeat   {};       // start position in beats from loop start
    gloopy::time::BeatRatio lengthBeats { 1, 1 }; // duration in beats
    float  velocity    { 0.8f };          // 0..1
    float  probability { 1.0f };          // 0..1 chance the note fires (generative; deterministic per repetition)
};
