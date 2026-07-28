// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>

// Browser drag-and-drop payload: a row dragged from the browser onto the arrangement
// carries "kind\tref\tlabel". A TAB separator (not a space/comma) is deliberate — the
// ref is often a file path or plugin identifier that contains spaces, so the parts must
// survive intact. Kept as pure functions so the format round-trip is unit-testable.
struct BrowserDragItem { juce::String kind, ref, label; bool valid { false }; };

inline juce::String makeBrowserDrag (const juce::String& kind, const juce::String& ref, const juce::String& label)
{
    return kind + "\t" + ref + "\t" + label;
}

inline BrowserDragItem parseBrowserDrag (const juce::String& desc)
{
    auto parts = juce::StringArray::fromTokens (desc, "\t", "");   // splits on tabs only; keeps spaces
    BrowserDragItem it;
    if (parts.size() >= 2 && parts[0].isNotEmpty() && parts[1].isNotEmpty())
    {
        it.kind  = parts[0];
        it.ref   = parts[1];
        it.label = parts.size() >= 3 ? parts[2] : parts[1];
        it.valid = true;
    }
    return it;
}

// When a browser row is dropped ON an existing track (targetTrack >= 0), a sample is placed
// as an audio clip at the drop beat instead of spawning a new track. Every other kind (and a
// drop that misses the track lanes) falls back to the generic "add to the project" action.
inline bool browserDropPlacesClip (const juce::String& kind, int targetTrack)
{
    return kind == "sample" && targetTrack >= 0;
}
