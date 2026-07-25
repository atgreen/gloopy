// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Mixer scenes — named snapshots of the mixer strip (per-insert volume/pan/mute/solo
// plus each effect's bypass state) that can be recalled later ("rough mix", "vocal
// up", "print mix"). This is *control* state, separate from automation: a scene is a
// static set of values, recalled on demand. Stored in the composition (see
// Composition.cpp) and serialised in toValueTree/loadFromTree (MainComponent.cpp).

#include "MainComponent.h"

bool MainComponent::apiDefineMixerScene (const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        MixerScene sc;
        sc.name = name;
        for (auto& mt : mixerTracks)
        {
            MixerScene::Insert in;
            in.volume = mt->volume.load(); in.pan = mt->pan.load();
            in.mute = mt->mute.load();     in.solo = mt->solo.load();
            for (auto& fx : mt->effects) in.bypass.push_back (fx->bypassed.load() ? 1 : 0);
            for (auto& s : mt->sends)    in.sends.push_back ({ s.bus, s.level });
            sc.inserts.push_back (std::move (in));
        }
        auto it = std::find_if (mixerScenes.begin(), mixerScenes.end(),
                                [&] (const MixerScene& s) { return s.name == name; });
        if (it != mixerScenes.end()) *it = std::move (sc);   // upsert
        else                          mixerScenes.push_back (std::move (sc));
        std::cout << "[scene] captured '" << name << "' (" << mixerTracks.size() << " inserts)" << std::endl;
        return true;
    });
}

std::vector<juce::String> MainComponent::apiListMixerScenes()
{
    return callOnMessageThread ([&]
    {
        const juce::ScopedLock sl (engineLock);
        std::vector<juce::String> out;
        for (auto& s : mixerScenes) out.push_back (s.name);
        return out;
    });
}

bool MainComponent::apiRecallMixerScene (const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        bool found = false;
        {
            const juce::ScopedLock sl (engineLock);
            auto it = std::find_if (mixerScenes.begin(), mixerScenes.end(),
                                    [&] (const MixerScene& s) { return s.name == name; });
            if (it == mixerScenes.end()) return false;
            found = true;
            // Apply what still matches the current mixer; tolerate added/removed
            // inserts or effects since the snapshot was taken.
            for (size_t i = 0; i < it->inserts.size() && i < mixerTracks.size(); ++i)
            {
                const auto& in = it->inserts[i];
                auto& mt = *mixerTracks[i];
                mt.volume.store (in.volume); mt.pan.store (in.pan);
                mt.mute.store (in.mute);     mt.solo.store (in.solo);
                for (size_t s = 0; s < in.bypass.size() && s < mt.effects.size(); ++s)
                    mt.effects[s]->bypassed.store (in.bypass[s] != 0);
                // Restore the level of each captured send that still exists (matched by
                // target bus); added/removed sends are tolerated, like inserts/effects.
                for (auto& cs : in.sends)
                    for (auto& s : mt.sends)
                        if (s.bus == cs.first) { s.level = cs.second; break; }
            }
        }
        emitChange ("effect_changed");   // nudge subscribers / UI to refresh the mixer
        std::cout << "[scene] recalled '" << name << "'" << std::endl;
        return found;
    });
}

bool MainComponent::apiRemoveMixerScene (const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        const auto before = mixerScenes.size();
        mixerScenes.erase (std::remove_if (mixerScenes.begin(), mixerScenes.end(),
                               [&] (const MixerScene& s) { return s.name == name; }),
                           mixerScenes.end());
        return mixerScenes.size() != before;
    });
}
