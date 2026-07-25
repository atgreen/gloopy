// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Controller mapping + MIDI-learn — bind a control source (a MIDI CC, an OSC name,
// or any string) to a ParamModel target so hardware knobs / OSC / scripts drive any
// parameter. Rides on the universal parameter model (Parameters.cpp) and the
// audio-thread-safe applyParamValue writer (Modulation.cpp). Feeds come from the MIDI
// callback, an OSC lane, or the SetController RPC — all through apiSetController, so
// the whole thing is verifiable headlessly without MIDI hardware.

#include "MainComponent.h"

bool MainComponent::apiAddControllerMap (const juce::String& source, const juce::String& target, float lo, float hi)
{
    if (source.trim().isEmpty() || target.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        auto it = std::find_if (controllerMaps.begin(), controllerMaps.end(),
                                [&] (const CtrlMap& m) { return m.source == source && m.target == target; });
        if (it != controllerMaps.end()) { it->lo = lo; it->hi = hi; }     // upsert
        else                             controllerMaps.push_back ({ source, target, lo, hi });
        std::cout << "[ctrl] map " << source << " -> " << target << " [" << lo << ".." << hi << "]" << std::endl;
        return true;
    });
}

bool MainComponent::apiRemoveControllerMap (const juce::String& source)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        const auto before = controllerMaps.size();
        controllerMaps.erase (std::remove_if (controllerMaps.begin(), controllerMaps.end(),
                                  [&] (const CtrlMap& m) { return m.source == source; }),
                              controllerMaps.end());
        return controllerMaps.size() != before;
    });
}

std::vector<MainComponent::CtrlMap> MainComponent::apiListControllerMaps()
{
    return callOnMessageThread ([&] { const juce::ScopedLock sl (engineLock); return controllerMaps; });
}

void MainComponent::apiMidiLearn (const juce::String& target)
{
    callOnMessageThread ([&] { const juce::ScopedLock sl (engineLock); learnTarget = target; return true; });
}

// Called from the MIDI callback / OSC lane / SetController RPC. value01 is 0..1.
// If a learn is armed, the first control captured binds to the pending target
// (scaled to that parameter's own min/max); otherwise every matching map is applied.
void MainComponent::apiSetController (const juce::String& source, float value01)
{
    // Capture branch (learn): resolve the target's range, add the map, done.
    juce::String learn;
    { const juce::ScopedLock sl (engineLock); learn = learnTarget; }
    if (learn.isNotEmpty())
    {
        ParamDesc d;
        const float lo = apiGetParameter (learn, d) ? d.min : 0.0f;
        const float hi = apiGetParameter (learn, d) ? d.max : 1.0f;
        apiAddControllerMap (source, learn, lo, hi);
        { const juce::ScopedLock sl (engineLock); learnTarget = {}; }
        return;
    }

    const float v01 = juce::jlimit (0.0f, 1.0f, value01);
    const juce::ScopedLock sl (engineLock);
    for (auto& m : controllerMaps)
        if (m.source == source && ! m.bypass)
            applyParamValue (m.target, m.lo + v01 * (m.hi - m.lo));   // lo>hi inverts; lock held -> audio-thread-safe
}

// Enable/disable a mapping without removing it (keyed by source+target).
bool MainComponent::apiSetControllerBypass (const juce::String& source, const juce::String& target, bool bypass)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        for (auto& m : controllerMaps)
            if (m.source == source && m.target == target) { m.bypass = bypass; return true; }
        return false;
    });
}
