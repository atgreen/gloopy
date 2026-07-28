// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Session-view control API — the OSC/gRPC surface for the clip-launch grid (Wave 8 slice 5).
// The launch state machine (SessionLauncher) and the grid model (Track::sessionSlots + scenes)
// already exist and are driven by the desktop SessionView; this file makes the same actions
// reachable over the control API so an agent/script can populate the grid, launch clips/scenes,
// stop, and read back what's playing/queued. All ops go to the message thread under engineLock
// (the launcher's advance() runs on the audio thread inside renderBlock, which holds the same
// lock, so requests never race the engine). Launches are *queued* — the launcher fires them at
// the next launch-quantum boundary during playback (its own logic is unit-tested separately).

#include "MainComponent.h"

// Copy an existing arrangement clip into a session slot (populate the grid over the API), so a
// script can build a launchable grid without the desktop drag. Reuses apiSetSessionClip, which
// grows the scene rows to reach `scene`.
bool MainComponent::apiCopyClipToSessionSlot (int trackId, int clipIndex, int scene)
{
    Clip copy;
    const bool have = callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        Track* t = resolveTrack (trackId);
        if (t == nullptr || ! juce::isPositiveAndBelow (clipIndex, (int) t->clips.size())) return false;
        copy = t->clips[(size_t) clipIndex];
        return true;
    });
    return have && apiSetSessionClip (trackId, scene, copy);
}

// Colour a session slot's clip (grid organization) — hex ARGB; empty clears the override so the
// slot draws in its track's colour again. Mirrors apiSetClipColour for arrangement clips.
bool MainComponent::apiSetSessionSlotColour (int trackId, int scene, const juce::String& hexArgb)
{
    const juce::String h = hexArgb.startsWith ("#") ? hexArgb.substring (1) : hexArgb;
    const juce::Colour col = h.isEmpty() ? juce::Colour ((juce::uint32) 0) : juce::Colour::fromString (h);
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        Track* t = resolveTrack (trackId);
        if (t == nullptr || scene < 0 || scene >= (int) t->sessionSlots.size()) return false;
        auto& clip = t->sessionSlots[(size_t) scene];
        if (clip == nullptr) return false;   // empty slot has no clip to colour
        clip->colour = col;                  // draw-only field; the audio thread never reads it
        return true;
    });
}

bool MainComponent::apiSessionLaunchClip (int trackId, int scene)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        int idx = -1;
        for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i]->id == trackId) { idx = i; break; }
        if (idx < 0 || scene < 0) return false;
        auto& slots = tracks[(size_t) idx]->sessionSlots;
        if (! (scene < (int) slots.size() && slots[(size_t) scene] != nullptr)) return false;   // empty slot: nothing to launch
        sessionLauncher.requestClip (idx, scene);
        return true;
    });
}

bool MainComponent::apiSessionLaunchScene (int scene)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        if (scene < 0) return false;
        std::vector<bool> occupied (tracks.size(), false);
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            auto& s = tracks[i]->sessionSlots;
            occupied[i] = (scene < (int) s.size() && s[(size_t) scene] != nullptr);
        }
        sessionLauncher.requestScene (scene, occupied);   // empty slots in the row are left untouched
        return true;
    });
}

bool MainComponent::apiSessionStopTrack (int trackId)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        int idx = -1;
        for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i]->id == trackId) { idx = i; break; }
        if (idx < 0) return false;
        sessionLauncher.requestStop (idx);
        return true;
    });
}

bool MainComponent::apiSessionStopAll()
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        sessionLauncher.requestStopAll();
        return true;
    });
}

MainComponent::SessionSnap MainComponent::apiGetSessionState()
{
    return callOnMessageThread ([&] () -> SessionSnap
    {
        const juce::ScopedLock sl (engineLock);
        SessionSnap snap;
        snap.scenes       = (int) scenes.size();
        snap.quantumBeats = sessionLauncher.quantumBeats();
        snap.anyPlaying   = sessionLauncher.anyPlaying();
        for (int i = 0; i < (int) tracks.size(); ++i)
            snap.tracks.push_back ({ tracks[(size_t) i]->id, sessionLauncher.playingSlot (i),
                                     sessionLauncher.pendingSlot (i), (int) tracks[(size_t) i]->sessionSlots.size() });
        return snap;
    });
}
