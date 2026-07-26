// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "Clip.h"

/** Session view (clip-launch grid) model.

    Columns are tracks, rows are global **scenes**; each track owns one clip slot per scene
    (`Track::sessionSlots`), a null entry meaning an empty slot. A session clip is just a `Clip`
    (which already loops and owns its content) living in a slot instead of on the timeline.

    The grid is kept **rectangular**: every track's slot column is the same length as the global
    scene list. The free functions below maintain that invariant as scenes are inserted/removed,
    and are pure so they unit-test without the engine. `std::shared_ptr<Clip>` lets the audio
    thread hold a launched clip safely while the message thread swaps slots. */
struct Scene
{
    juce::String name;
    juce::Colour colour {};      // ARGB 0 (default) = inherit / no override
};

using SessionSlots = std::vector<std::shared_ptr<Clip>>;

/** Null-pad (or truncate) a track's slot column to exactly `sceneCount` slots. New slots are
    empty (null). Call when a track is added or the scene count changes. */
inline void ensureSlotCount (SessionSlots& slots, int sceneCount)
{
    slots.resize ((size_t) juce::jmax (0, sceneCount));          // grown slots default to null (empty)
}

/** Insert an empty slot at scene index `at` (clamped), shifting later slots down — the per-track
    half of "insert a scene row". */
inline void insertSceneSlot (SessionSlots& slots, int at)
{
    const int i = juce::jlimit (0, (int) slots.size(), at);
    slots.insert (slots.begin() + i, nullptr);
}

/** Remove the slot at scene index `at` (if in range) — the per-track half of "delete a scene row". */
inline void removeSceneSlot (SessionSlots& slots, int at)
{
    if (at >= 0 && at < (int) slots.size()) slots.erase (slots.begin() + at);
}

/** The clip in a slot, or nullptr if the slot is empty or the index is out of range. */
inline std::shared_ptr<Clip> slotClip (const SessionSlots& slots, int scene)
{
    return (scene >= 0 && scene < (int) slots.size()) ? slots[(size_t) scene] : nullptr;
}
