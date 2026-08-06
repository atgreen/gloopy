// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Control groups (VCA-lite) — a named group whose fader SCALES the volumes of its
// member inserts, without any extra audio routing. Each member insert carries the
// group's name on its MixerTrack (so membership survives insert re-indexing); the
// group holds the shared gain + mute. The mix loop (renderBlock, MainComponent.cpp)
// multiplies each insert's volume by its group's gain and silences it if the group
// is muted. This is *control scaling* (like an Ardour VCA / a console group fader),
// distinct from a bus, which is real summing routing. Serialised in
// toValueTree/loadFromTree and the composition (Composition.cpp).

#include "MainComponent.h"
#include "EngineLock.h"

MainComponent::ControlGroup* MainComponent::findControlGroup (const juce::String& name)
{
    for (auto& g : controlGroups)
        if (g->name == name) return g.get();
    return nullptr;
}

bool MainComponent::apiDefineControlGroup (const juce::String& name, float gain)
{
    if (name.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        if (auto* existing = findControlGroup (name))
            existing->gain.store (juce::jlimit (0.0f, 1.0f, gain));
        else
        {
            auto ng = std::make_unique<ControlGroup>();
            ng->name = name;
            ng->gain.store (juce::jlimit (0.0f, 1.0f, gain));
            controlGroups.push_back (std::move (ng));
        }
        return true;
    });
}

bool MainComponent::apiSetControlGroupGain (const juce::String& name, float gain)
{
    return callOnMessageThread ([&] () -> bool
    {
        GLOOPY_ELOCK(sl);
        auto* g = findControlGroup (name);
        if (g == nullptr) return false;
        g->gain.store (juce::jlimit (0.0f, 1.0f, gain));
        return true;
    });
}

bool MainComponent::apiSetControlGroupMute (const juce::String& name, bool mute)
{
    return callOnMessageThread ([&] () -> bool
    {
        GLOOPY_ELOCK(sl);
        auto* g = findControlGroup (name);
        if (g == nullptr) return false;
        g->mute.store (mute);
        return true;
    });
}

bool MainComponent::apiSetControlGroupSolo (const juce::String& name, bool solo)
{
    return callOnMessageThread ([&] () -> bool
    {
        GLOOPY_ELOCK(sl);
        auto* g = findControlGroup (name);
        if (g == nullptr) return false;
        g->solo.store (solo);
        return true;
    });
}

// Assign an insert to a group (group="" clears its membership). Creating a reference
// to a not-yet-defined group defines it (unity gain), so a single call both makes a
// group and adds its first member.
bool MainComponent::apiAssignInsertToGroup (int insert, const juce::String& group)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return false;
        const juce::String g = group.trim();
        if (g.isNotEmpty() && findControlGroup (g) == nullptr)
        {
            auto ng = std::make_unique<ControlGroup>();
            ng->name = g;
            controlGroups.push_back (std::move (ng));
        }
        mixerTracks[(size_t) insert]->group = g;
        return true;
    });
}

bool MainComponent::apiRemoveControlGroup (const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        const auto before = controlGroups.size();
        for (auto& mt : mixerTracks)
            if (mt->group == name) mt->group.clear();
        controlGroups.erase (std::remove_if (controlGroups.begin(), controlGroups.end(),
                                             [&] (const std::unique_ptr<ControlGroup>& g) { return g->name == name; }),
                             controlGroups.end());
        return controlGroups.size() != before;
    });
}

std::vector<MainComponent::ControlGroupInfo> MainComponent::apiListControlGroups()
{
    return callOnMessageThread ([&]
    {
        GLOOPY_ELOCK(sl);
        std::vector<ControlGroupInfo> out;
        for (auto& g : controlGroups)
        {
            int members = 0;
            for (auto& mt : mixerTracks)
                if (mt->group == g->name) ++members;
            out.push_back ({ g->name, g->gain.load(), g->mute.load(), g->solo.load(), members });
        }
        return out;
    });
}
