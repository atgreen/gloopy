// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

/** Session-view launch engine — the pure state machine behind the clip-launch grid.

    Tracks, per project track, which session slot (scene index) is currently *playing* and which
    (if any) is *pending* (queued). Launches are quantized: a request set between blocks is applied
    at the next launch-quantum boundary (default 1 bar) when the transport beat clock crosses it,
    so everything triggers in sync. Playback model is **per-track override**: a track with
    `playingSlot >= 0` plays that session clip (looped) instead of its arrangement clips; -1 means
    "play the arrangement". Launching a clip on a track replaces whatever it was playing
    (one clip per track); launching a scene fires the whole row.

    This class owns no audio and no clips — it only decides *which slot plays and since when*.
    The render loop reads `playingSlot`/`launchBeat` to play the right content, and
    `changedTracks()` to silence a track whose slot just changed. State is guarded by the engine
    lock (requests on the message thread, `advance()` on the audio thread, same as the rest of the
    engine), so no atomics are needed. Pure + deterministic → unit-testable without the engine. */
class SessionLauncher
{
public:
    static constexpr int kNone = -2;   // no pending request
    static constexpr int kStop = -1;   // pending: stop (back to arrangement)
    static constexpr int kArrangement = -1;   // playingSlot value meaning "play the arrangement"

    /** Resize per-track state to match the project's track count (call under the engine lock when
        tracks are added/removed). Existing state is preserved for surviving indices. */
    void setTrackCount (int n)
    {
        st.resize ((size_t) juce::jmax (0, n));
    }
    int trackCount() const noexcept { return (int) st.size(); }

    void   setQuantumBeats (double q) noexcept { quantum = juce::jmax (0.0, q); }   // 0 = launch immediately
    double quantumBeats() const noexcept { return quantum; }

    // --- Requests (message thread, under the engine lock). Applied at the next quantum boundary. ---

    /** Queue a launch of `scene` on `track` (caller ensures the slot is non-empty). */
    void requestClip (int track, int scene)
    {
        if (valid (track) && scene >= 0) st[(size_t) track].pending = scene;
    }
    /** Queue a stop on `track` (returns it to arrangement playback). */
    void requestStop (int track)
    {
        if (valid (track)) st[(size_t) track].pending = kStop;
    }
    /** Queue a stop on every track. */
    void requestStopAll()
    {
        for (auto& s : st) s.pending = kStop;
    }
    /** Queue a scene launch: fire `scene` on every track whose slot in that row is occupied.
        Empty slots leave their track unchanged (no implicit stop). `occupied[t]` = slot has a clip. */
    void requestScene (int scene, const std::vector<bool>& occupied)
    {
        if (scene < 0) return;
        for (int i = 0; i < (int) st.size(); ++i)
            if (i < (int) occupied.size() && occupied[(size_t) i])
                st[(size_t) i].pending = scene;
    }

    /** Advance the quantized clock over [beatStart, beatEnd) and apply any pending requests whose
        boundary falls in that range. Fills `changedTracks()` with the indices whose playing slot
        changed (or re-launched) this call — the render loop uses it to note-off those tracks. */
    void advance (double beatStart, double beatEnd)
    {
        justChanged.clear();
        bool anyPending = false;
        for (auto& s : st) if (s.pending != kNone) { anyPending = true; break; }
        if (! anyPending) return;

        if (quantum <= 0.0)                                    // immediate
        {
            for (int i = 0; i < (int) st.size(); ++i) applyPending (i, beatStart);
            return;
        }
        // First launch-quantum boundary at or after beatStart. Blocks are far shorter than a
        // quantum, so at most one boundary falls in the range.
        const double eps = 1e-9;
        const double b   = std::ceil (beatStart / quantum - eps) * quantum;
        if (b < beatEnd - eps)
            for (int i = 0; i < (int) st.size(); ++i) applyPending (i, b);
    }

    /** Immediately stop everything (panic / project load / transport reset). Clears all state. */
    void reset()
    {
        justChanged.clear();
        for (auto& s : st) { s = {}; }
    }

    // --- Queries (render loop / UI). ---
    int    playingSlot (int track) const { return valid (track) ? st[(size_t) track].playing : kArrangement; }
    int    pendingSlot (int track) const { return valid (track) ? st[(size_t) track].pending : kNone; }
    double launchBeat  (int track) const { return valid (track) ? st[(size_t) track].launchBeat : 0.0; }
    bool   anyPlaying() const { for (auto& s : st) if (s.playing >= 0) return true; return false; }
    const std::vector<int>& changedTracks() const noexcept { return justChanged; }

private:
    struct S { int playing { kArrangement }; int pending { kNone }; double launchBeat { 0.0 }; };

    bool valid (int t) const noexcept { return t >= 0 && t < (int) st.size(); }

    void applyPending (int i, double atBeat)
    {
        auto& s = st[(size_t) i];
        if (s.pending == kNone) return;
        if (s.pending == kStop)
        {
            if (s.playing != kArrangement) { s.playing = kArrangement; justChanged.push_back (i); }
        }
        else                                                   // launch (>=0): always (re)trigger from the top
        {
            s.playing = s.pending;
            s.launchBeat = atBeat;
            justChanged.push_back (i);
        }
        s.pending = kNone;
    }

    std::vector<S>   st;
    double           quantum { 4.0 };   // 1 bar @ 4/4
    std::vector<int> justChanged;
};
