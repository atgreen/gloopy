// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <atomic>

// Audio-thread allocation guard. The audio thread must never touch the heap in the mix
// (principle 4). To PROVE that headlessly we replace the global operator new/new[] (see
// AudioAllocGuard.cpp) with a thin malloc pass-through that increments a counter ONLY while
// the audio thread is inside the mix — everywhere else it is a plain allocation with a single
// thread-local bool check. getNextAudioBlock wraps the renderBlock call in an AudioThreadGuard;
// GetDiagnostics exposes the count. A steady-state delta of 0 during playback is the proof.
namespace gloopy
{
    extern thread_local bool     g_onAudioThread;    // true only while the audio thread is in the mix
    extern std::atomic<long long> g_audioAllocCount;  // heap allocations seen while g_onAudioThread

    // RAII: arm the counter for the duration of the mix on the audio thread.
    struct AudioThreadGuard
    {
        AudioThreadGuard()  { g_onAudioThread = true; }
        ~AudioThreadGuard() { g_onAudioThread = false; }
        AudioThreadGuard (const AudioThreadGuard&) = delete;
        AudioThreadGuard& operator= (const AudioThreadGuard&) = delete;
    };

    inline long long audioAllocCount() { return g_audioAllocCount.load (std::memory_order_relaxed); }
}
