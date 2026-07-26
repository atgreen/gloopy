// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Buses & sends — minimal aux-send routing. A bus is just another mixer track that
// receives aux sends and sums to master (like every insert). A send is an additive,
// post-effects tap from an insert into a bus at a level (see the mix loop in
// MainComponent::renderBlock). Buses are appended after the regular inserts so their
// buffers accumulate sends before they're processed in the summing loop.

#include "MainComponent.h"

int MainComponent::apiAddBus (const juce::String& name)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        int index = -1;
        {
            const juce::ScopedLock sl (engineLock);
            auto bus = std::make_unique<MixerTrack> (name.isNotEmpty() ? name : "Bus");
            bus->isBus = true;
            bus->buffer.setSize (2, juce::jmax (16, currentBlockSize));
            mixerTracks.push_back (std::move (bus));
            index = (int) mixerTracks.size() - 1;
        }
        emitChange ("effect_changed", -1, index);
        if (mixerView) mixerView->rebuild();
        std::cout << "[bus] added '" << name << "' at insert " << index << std::endl;
        return index;
    });
}

// Remove a bus mixer track (and only a bus — not master or a regular insert). Sends are
// re-indexed: any send targeting the removed bus is dropped, and sends targeting a higher-
// indexed track shift down by one (mixerTracks indices are the send/insert address space).
bool MainComponent::apiRemoveBus (int busIndex)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        bool ok = false;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (busIndex, (int) mixerTracks.size())) return false;
            if (! mixerTracks[(size_t) busIndex]->isBus) return false;   // buses only
            mixerTracks.erase (mixerTracks.begin() + busIndex);
            for (auto& mt : mixerTracks)
            {
                auto& sends = mt->sends;
                sends.erase (std::remove_if (sends.begin(), sends.end(),
                                 [&] (const MixerTrack::Send& s) { return s.bus == busIndex; }), sends.end());
                for (auto& s : sends) if (s.bus > busIndex) --s.bus;
            }
            ok = true;
        }
        emitChange ("effect_changed", -1, busIndex);
        if (mixerView) mixerView->rebuild();
        std::cout << "[bus] removed insert " << busIndex << std::endl;
        return ok;
    });
}

bool MainComponent::apiSetSend (int insert, int bus, float level, bool postFader)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return false;
        if (! juce::isPositiveAndBelow (bus,    (int) mixerTracks.size())) return false;
        if (bus == insert || bus == 0) return false;   // no self-send, and master isn't a send target

        auto& sends = mixerTracks[(size_t) insert]->sends;
        auto it = std::find_if (sends.begin(), sends.end(),
                                [&] (const MixerTrack::Send& s) { return s.bus == bus; });
        if (level <= 0.0f)   // remove
        {
            if (it == sends.end()) return false;
            sends.erase (it);
        }
        else if (it != sends.end()) { it->level = level; it->postFader = postFader; }   // update
        else                        sends.push_back ({ bus, level, postFader });
        return true;
    });
}
