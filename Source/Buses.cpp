// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Buses & sends — minimal aux-send routing. A bus is just another mixer track that
// receives aux sends and sums to master (like every insert). A send is an additive,
// post-effects tap from an insert into a bus at a level (see the mix loop in
// MainComponent::renderBlock). Buses are appended after the regular inserts so their
// buffers accumulate sends before they're processed in the summing loop.

#include "MainComponent.h"
#include "EngineLock.h"

int MainComponent::apiAddBus (const juce::String& name)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        int index = -1;
        {
            GLOOPY_ELOCK(sl);
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
            GLOOPY_ELOCK(sl);
            if (! juce::isPositiveAndBelow (busIndex, (int) mixerTracks.size())) return false;
            if (! mixerTracks[(size_t) busIndex]->isBus) return false;   // buses only
            mixerTracks.erase (mixerTracks.begin() + busIndex);
            for (auto& mt : mixerTracks)
            {
                auto& sends = mt->sends;
                sends.erase (std::remove_if (sends.begin(), sends.end(),
                                 [&] (const MixerTrack::Send& s) { return s.bus == busIndex; }), sends.end());
                for (auto& s : sends) if (s.bus > busIndex) --s.bus;
                // Main-output routing follows the same index space as sends.
                const int o = mt->output.load();
                if      (o == busIndex) mt->output.store (0);        // routed to the removed bus -> master
                else if (o >  busIndex) mt->output.store (o - 1);    // shifted down by the erase
            }
            ok = true;
        }
        emitChange ("effect_changed", -1, busIndex);
        if (mixerView) mixerView->rebuild();
        std::cout << "[bus] removed insert " << busIndex << std::endl;
        return ok;
    });
}

// Ungroup: dissolve a group bus. Its members are reparented to the group's own output (its parent
// group, or master for a top-level group — so ungrouping an INNER group keeps its tracks in the
// outer one), then the bus is removed. Mirrors apiRemoveBus's re-indexing but reroutes members to
// the parent instead of blanket-to-master.
bool MainComponent::apiUngroup (int busIndex)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        bool ok = false;
        {
            GLOOPY_ELOCK(sl);
            if (! juce::isPositiveAndBelow (busIndex, (int) mixerTracks.size())) return false;
            if (! mixerTracks[(size_t) busIndex]->isBus) return false;
            const int parent    = mixerTracks[(size_t) busIndex]->output.load();   // group's parent (0 = master)
            const int newParent = (parent > busIndex) ? parent - 1 : 0;            // its index after the erase (parent is 0 or higher)
            mixerTracks.erase (mixerTracks.begin() + busIndex);
            for (auto& mt : mixerTracks)
            {
                auto& sends = mt->sends;
                sends.erase (std::remove_if (sends.begin(), sends.end(),
                                 [&] (const MixerTrack::Send& s) { return s.bus == busIndex; }), sends.end());
                for (auto& s : sends) if (s.bus > busIndex) --s.bus;
                const int o = mt->output.load();
                if      (o == busIndex) mt->output.store (newParent);   // member -> the group's parent
                else if (o >  busIndex) mt->output.store (o - 1);       // shifted down by the erase
            }
            ok = true;
        }
        emitChange ("effect_changed", -1, busIndex);
        if (mixerView) mixerView->rebuild();
        std::cout << "[bus] ungrouped insert " << busIndex << std::endl;
        return ok;
    });
}

bool MainComponent::apiSetSend (int insert, int bus, float level, bool postFader)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
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

// Route an insert's MAIN output into another mixer track: 0 = master (default), else a group/bus
// (a submix). Unlike a send (a parallel copy), this is the insert's whole output — it no longer
// sums to master directly. v1 constraint: the target must be a bus and higher-indexed than the
// insert (so it's processed later in the flat summing loop and accumulates first).
bool MainComponent::apiSetInsertOutput (int insert, int target)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return false;
        if (insert == 0) return false;                       // master has no output target
        if (target != 0)                                     // 0 = master, always valid
        {
            if (! juce::isPositiveAndBelow (target, (int) mixerTracks.size())) return false;
            if (! mixerTracks[(size_t) target]->isBus) return false;   // v1: only master or a bus
            if (target <= insert) return false;              // must be summed later in the loop
        }
        mixerTracks[(size_t) insert]->output.store (target);
        return true;
    });
}

// Gather a group's member tracks contiguous (Ableton-style: grouping reorders tracks so the group
// is a solid block). Members = tracks whose insert routes its main output to `busIndex`; they are
// clustered at the first member's slot, everyone keeping relative order. No-op if <2 members or
// already contiguous. Shared arrangement order, so this moves them in the arrange view too.
bool MainComponent::apiGatherGroup (int busIndex)
{
    return callOnMessageThread ([&] () -> bool
    {
        // Transitive: a track is a member if busIndex is anywhere in its insert's output chain
        // (so gathering an outer group also pulls in the tracks of its nested sub-groups).
        auto isMember = [&] (int t) -> bool
        {
            const int N = (int) mixerTracks.size();
            int cur = tracks[(size_t) t]->mixerTrack.load();
            for (int guard = 0; guard < N && juce::isPositiveAndBelow (cur, N); ++guard)
            {
                const int o = mixerTracks[(size_t) cur]->output.load();
                if (o == busIndex) return true;
                if (o <= 0 || o >= N || o == cur) break;
                cur = o;
            }
            return false;
        };
        // Decide whether a gather is needed without mutating (so we don't snapshot a no-op).
        {
            GLOOPY_ELOCK(sl);
            if (! juce::isPositiveAndBelow (busIndex, (int) mixerTracks.size())) return false;
            const int n = (int) tracks.size();
            int count = 0, firstAt = -1;
            for (int t = 0; t < n; ++t) if (isMember (t)) { ++count; if (firstAt < 0) firstAt = t; }
            if (count < 2) return false;
            bool contig = true;
            for (int t = firstAt; t < firstAt + count; ++t) if (t >= n || ! isMember (t)) { contig = false; break; }
            if (contig) return false;
        }
        pushUndoSnapshot();
        {
            GLOOPY_ELOCK(sl);
            std::vector<std::unique_ptr<Track>> mems, result;
            int insertAt = -1;
            for (int t = 0; t < (int) tracks.size(); ++t)
            {
                if (isMember (t)) { if (insertAt < 0) insertAt = (int) result.size(); mems.push_back (std::move (tracks[(size_t) t])); }
                else               result.push_back (std::move (tracks[(size_t) t]));
            }
            result.insert (result.begin() + insertAt, std::make_move_iterator (mems.begin()), std::make_move_iterator (mems.end()));
            tracks = std::move (result);
        }
        emitChange ("track_moved", -1);
        if (arrangeView) arrangeView->rebuild();
        if (sessionPane) sessionPane->rebuild();
        resized();
        return true;
    });
}

// The "Group" gesture: create a bus and route each member insert's main output into it. Returns
// the new bus index (or -1). Members must be regular inserts (not master); the bus is appended so
// it always outranks them.
int MainComponent::apiGroupInserts (const std::vector<int>& inserts, const juce::String& name)
{
    const int bus = apiAddBus (name.isNotEmpty() ? name : "Group");   // appended -> highest index
    if (bus < 0) return -1;
    for (int i : inserts)
        if (i > 0 && i < bus) apiSetInsertOutput (i, bus);
    if (mixerView) mixerView->rebuild();
    return bus;
}

// --- dynamic per-track inserts -------------------------------------------------------------
// Each track owns one insert; buses always sit above the track inserts. These keep the routing
// index space (sends.bus, MixerTrack::output, Track::mixerTrack) consistent when an insert is
// added or removed. Callers hold engineLock (it is recursive, so re-locking is harmless).

int MainComponent::firstBusIndex() const
{
    for (int i = 1; i < (int) mixerTracks.size(); ++i)
        if (mixerTracks[(size_t) i]->isBus) return i;
    return (int) mixerTracks.size();
}

void MainComponent::insertMixerTrackAt (int pos, std::unique_ptr<MixerTrack> mt)
{
    pos = juce::jlimit (1, (int) mixerTracks.size(), pos);   // never before Master
    mixerTracks.insert (mixerTracks.begin() + pos, std::move (mt));
    for (auto& m : mixerTracks)
    {
        for (auto& s : m->sends) if (s.bus >= pos) ++s.bus;             // send targets shifted up
        const int o = m->output.load(); if (o >= pos) m->output.store (o + 1);   // 0 (master) stays 0
    }
    for (auto& t : tracks) { const int mi = t->mixerTrack.load(); if (mi >= pos) t->mixerTrack.store (mi + 1); }
}

void MainComponent::removeMixerTrackAt (int pos)
{
    if (! juce::isPositiveAndBelow (pos, (int) mixerTracks.size()) || pos == 0) return;   // never remove Master
    mixerTracks.erase (mixerTracks.begin() + pos);
    for (auto& m : mixerTracks)
    {
        auto& sends = m->sends;
        sends.erase (std::remove_if (sends.begin(), sends.end(),
                        [&] (const MixerTrack::Send& s) { return s.bus == pos; }), sends.end());   // sends to it dropped
        for (auto& s : sends) if (s.bus > pos) --s.bus;
        const int o = m->output.load();
        if      (o == pos) m->output.store (0);        // routed to the removed insert -> master
        else if (o >  pos) m->output.store (o - 1);
    }
    for (auto& t : tracks)
    { const int mi = t->mixerTrack.load(); if (mi == pos) t->mixerTrack.store (0); else if (mi > pos) t->mixerTrack.store (mi - 1); }
}

// Legacy projects (and the old startup) carried a fixed pool of 8 "Ins N" strips; drop any non-bus
// insert that no track uses, so a loaded project ends up with exactly Master + per-track inserts + buses.
void MainComponent::pruneUnbackedInserts()
{
    GLOOPY_ELOCK(sl);
    for (int i = (int) mixerTracks.size() - 1; i >= 1; --i)   // high -> low so indices below stay stable
    {
        if (mixerTracks[(size_t) i]->isBus) continue;
        bool backed = false;
        for (auto& t : tracks) if (t->mixerTrack.load() == i) { backed = true; break; }
        if (! backed) removeMixerTrackAt (i);
    }
}

// A "group" is a bus that at least one insert routes its main output into (i.e. shows as a session
// group column). Fold/unfold every such bus at once.
void MainComponent::foldAllGroups (bool fold)
{
    {
        GLOOPY_ELOCK(sl);
        std::vector<char> hasMember (mixerTracks.size(), 0);
        for (auto& mt : mixerTracks)
        {
            const int o = mt->output.load();
            if (o > 0 && o < (int) mixerTracks.size()) hasMember[(size_t) o] = 1;
        }
        for (size_t i = 0; i < mixerTracks.size(); ++i)
            if (mixerTracks[i]->isBus && hasMember[i]) mixerTracks[i]->folded.store (fold);
    }
    if (sessionPane) sessionPane->rebuild();
}

void MainComponent::toggleFoldAllGroups()
{
    bool anyOpen = false;
    {
        GLOOPY_ELOCK(sl);
        std::vector<char> hasMember (mixerTracks.size(), 0);
        for (auto& mt : mixerTracks)
        {
            const int o = mt->output.load();
            if (o > 0 && o < (int) mixerTracks.size()) hasMember[(size_t) o] = 1;
        }
        for (size_t i = 0; i < mixerTracks.size(); ++i)
            if (mixerTracks[i]->isBus && hasMember[i] && ! mixerTracks[i]->folded.load()) { anyOpen = true; break; }
    }
    foldAllGroups (anyOpen);   // any group open -> fold them all; otherwise unfold all
}
