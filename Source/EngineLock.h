// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

// Engine-lock holder instrumentation (RT-safety epic, slice 1).
//
// The audio callback acquires `engineLock` with a try-lock and drops the block to silence
// on a miss (a dropout -> audible crackle; counted in diagDropouts). A miss only happens
// because some OTHER thread is holding engineLock at that instant. To turn each miss into a
// NAMED culprit instead of a fresh investigation, every blocking holder records WHERE it
// took the lock (file:line) and WHEN; on a miss the audio thread reads that back. This is
// Hydrogen's m_LockingThread / m_pLocker idea (see src/core/AudioEngine/AudioEngine.h).
//
// All state here is plain relaxed atomics — no lock, no allocation — so the audio thread
// reads it safely. Holders go through GLOOPY_ELOCK instead of juce::ScopedLock; the macro
// stamps __FILE__ ":" __LINE__ (a static string literal, so the pointer is always valid).
namespace gloopy
{
    struct EngineLockTrace
    {
        // current holder (null site == lock free)
        std::atomic<const char*> holderSite   { nullptr };
        std::atomic<std::uint64_t> acquireTicks { 0 };
        std::atomic<std::uint64_t> holderThread { 0 };
        // worst hold seen this session (updated on release)
        std::atomic<const char*> maxHoldSite  { nullptr };
        std::atomic<double>      maxHoldUs     { 0.0 };
        // last try-lock miss (updated on a dropout): who held it, and for how long so far
        std::atomic<const char*> lastMissSite   { nullptr };
        std::atomic<double>      lastMissHeldUs { 0.0 };
        std::atomic<long long>   missCount      { 0 };
    };

    inline EngineLockTrace g_engineLockTrace;                 // C++17 inline global — header-only
    inline thread_local int g_engineLockDepth = 0;           // recursion depth (record outermost only)

    // RAII replacement for juce::ScopedLock(engineLock) that records the holder. Behaves
    // exactly like ScopedLock (enter on construct, exit on destruct; the CriticalSection is
    // recursive) plus the bookkeeping. Only the outermost acquisition on a thread records,
    // so a recursive re-entry doesn't clobber the real acquire time.
    struct ScopedEngineLock
    {
        // Hold a const ref like juce::ScopedLock does — CriticalSection::enter()/exit() are
        // const, so a lock taken inside a const method (a const engineLock ref) still works.
        const juce::CriticalSection& cs;
        ScopedEngineLock (const juce::CriticalSection& c, const char* here) : cs (c)
        {
            cs.enter();
            if (g_engineLockDepth++ == 0)
            {
                g_engineLockTrace.holderSite.store (here, std::memory_order_relaxed);
                g_engineLockTrace.acquireTicks.store ((std::uint64_t) juce::Time::getHighResolutionTicks(), std::memory_order_relaxed);
                g_engineLockTrace.holderThread.store ((std::uint64_t) (juce::pointer_sized_int) juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);
            }
        }
        ~ScopedEngineLock()
        {
            if (--g_engineLockDepth == 0)
            {
                const auto t0 = g_engineLockTrace.acquireTicks.load (std::memory_order_relaxed);
                const double us = juce::Time::highResolutionTicksToSeconds (
                                      (juce::int64) ((std::uint64_t) juce::Time::getHighResolutionTicks() - t0)) * 1.0e6;
                if (us > g_engineLockTrace.maxHoldUs.load (std::memory_order_relaxed))
                {
                    g_engineLockTrace.maxHoldUs.store (us, std::memory_order_relaxed);
                    g_engineLockTrace.maxHoldSite.store (g_engineLockTrace.holderSite.load (std::memory_order_relaxed), std::memory_order_relaxed);
                }
                g_engineLockTrace.holderSite.store (nullptr, std::memory_order_relaxed);
            }
            cs.exit();
        }
        ScopedEngineLock (const ScopedEngineLock&) = delete;
        ScopedEngineLock& operator= (const ScopedEngineLock&) = delete;
    };

    // Call from a try-lock MISS (audio callback dropout): snapshot who held the lock and how
    // long they'd held it. Lock-free; safe on the audio thread.
    inline void noteEngineLockMiss()
    {
        auto* site = g_engineLockTrace.holderSite.load (std::memory_order_relaxed);
        const auto t0 = g_engineLockTrace.acquireTicks.load (std::memory_order_relaxed);
        const double heldUs = site != nullptr
            ? juce::Time::highResolutionTicksToSeconds ((juce::int64) ((std::uint64_t) juce::Time::getHighResolutionTicks() - t0)) * 1.0e6
            : 0.0;
        g_engineLockTrace.lastMissSite.store (site, std::memory_order_relaxed);
        g_engineLockTrace.lastMissHeldUs.store (heldUs, std::memory_order_relaxed);
        g_engineLockTrace.missCount.fetch_add (1, std::memory_order_relaxed);
    }
}

#define GLOOPY_ELOCK_STR2(x) #x
#define GLOOPY_ELOCK_STR(x)  GLOOPY_ELOCK_STR2(x)
#define GLOOPY_HERE          (__FILE__ ":" GLOOPY_ELOCK_STR(__LINE__))
// Drop-in for `GLOOPY_ELOCK(var);`
#define GLOOPY_ELOCK(var)        const gloopy::ScopedEngineLock var (engineLock, GLOOPY_HERE)
// Variant for a differently-scoped lock reference (e.g. owner.engineLock, *hooks.engineLock)
#define GLOOPY_ELOCK_ON(var, l)  const gloopy::ScopedEngineLock var ((l), GLOOPY_HERE)
