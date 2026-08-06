// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Timeline locations — named points and ranges on the timeline (markers, sections,
// loop/punch/export ranges). A single model so the ruler, the control API, and
// render/export presets all refer to the same named spots ("intro", "chorus-2",
// "loop-a") instead of re-passing raw beat numbers. Stored in the composition
// (see Composition.cpp) and serialised in toValueTree/loadFromTree (MainComponent.cpp).

#include "MainComponent.h"
#include "EngineLock.h"

using gloopy::time::BeatPosition;

bool MainComponent::apiAddLocation (const juce::String& name, const juce::String& kind,
                                    double startBeat, double endBeat)
{
    if (name.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        const double s = juce::jmax (0.0, startBeat);
        const double e = juce::jmax (s, endBeat);   // markers pass end<=start -> collapses to a point
        pushUndoSnapshot();
        {
            GLOOPY_ELOCK(sl);
            auto it = std::find_if (locations.begin(), locations.end(),
                                    [&] (const TimelineLocation& l) { return l.name == name; });
            if (it != locations.end())
                *it = { name, kind, BeatPosition { s }, BeatPosition { e } };                       // upsert
            else
                locations.push_back ({ name, kind, BeatPosition { s }, BeatPosition { e } });
        }
        std::cout << "[loc] " << kind << " '" << name << "' [" << s << ".." << e << "]" << std::endl;
        return true;
    });
}

std::vector<MainComponent::TimelineLocation> MainComponent::apiListLocations()
{
    // No engineLock: `locations` (timeline cue/section markers) is message-thread-only state —
    // mutated only via the message-thread Locations API + load, and the audio thread never reads
    // it (loop points live on the transport, not here). This body already runs on the message
    // thread via callOnMessageThread, so the read is serialised with every writer. Taking
    // engineLock here fired at UI-repaint rate (the arrange view lists markers each frame) and
    // showed up as a steady-state dropout source in the slice-1 trace — same shape as meterMap.
    return callOnMessageThread ([&] { return locations; });
}

bool MainComponent::apiRemoveLocation (const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        const auto before = locations.size();
        locations.erase (std::remove_if (locations.begin(), locations.end(),
                             [&] (const TimelineLocation& l) { return l.name == name; }),
                         locations.end());
        return locations.size() != before;
    });
}

// Resolve a named range/section to a beat span, for render/export targeting.
// A zero-length location (a marker) is rejected — a range must span time.
bool MainComponent::apiResolveRange (const juce::String& name, double& startBeat, double& endBeat)
{
    GLOOPY_ELOCK(sl);
    for (auto& l : locations)
        if (l.name == name && l.endBeat > l.startBeat)          // typed position comparison (Time.h)
        {
            startBeat = l.startBeat.inBeats();
            endBeat   = l.endBeat.inBeats();
            return true;
        }
    return false;
}
