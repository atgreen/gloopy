// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "Note.h"

// Clip notes <-> JSON, factored out so it's unit-testable without the engine and shared
// by the ExportNotesJSON/ImportNotesJSON RPCs and the desktop copy/paste-notes gestures.
// Shape: a JSON array of {pitch, start, length, velocity} (beats; velocity 0..1). Parsing
// is tolerant: missing keys take Note defaults, values are clamped, and a top-level
// {"notes":[...]} wrapper is accepted as well as a bare array.

inline juce::String notesToJson (const std::vector<Note>& notes)
{
    juce::Array<juce::var> arr;
    for (const auto& n : notes)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("pitch",    n.pitch);
        o->setProperty ("start",    n.startBeat.toBeats());
        o->setProperty ("length",   n.lengthBeats.toBeats());
        o->setProperty ("velocity", (double) n.velocity);
        if (n.probability < 1.0f) o->setProperty ("probability", (double) n.probability);
        arr.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (arr), true);   // one line (clipboard-friendly)
}

inline std::vector<Note> notesFromJson (const juce::String& json)
{
    std::vector<Note> out;
    const juce::var v = juce::JSON::parse (json);

    const juce::Array<juce::var>* arr = v.getArray();
    if (arr == nullptr && v.isObject())                 // accept {"notes":[...]}
        if (auto nv = v.getProperty ("notes", {}); nv.isArray())
            arr = nv.getArray();
    if (arr == nullptr) return out;

    for (const auto& e : *arr)
    {
        if (! e.isObject()) continue;
        Note n;
        n.pitch       = juce::jlimit (0, 127, (int) e.getProperty ("pitch", 60));
        n.startBeat   = (double) e.getProperty ("start",  e.getProperty ("startBeat", 0.0));
        n.lengthBeats = juce::jmax (0.0, (double) e.getProperty ("length", e.getProperty ("lengthBeats", 1.0)));
        n.velocity    = juce::jlimit (0.0f, 1.0f, (float) (double) e.getProperty ("velocity", 0.8));
        n.probability = juce::jlimit (0.0f, 1.0f, (float) (double) e.getProperty ("probability", 1.0));
        out.push_back (n);
    }
    return out;
}
