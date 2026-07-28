// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Global operator new/delete replacement backing the audio-thread allocation guard.
// Off the audio thread this is a plain malloc/free pass-through (one thread-local bool
// check of cost); while g_onAudioThread is set (the mix, on the audio thread) each heap
// allocation bumps g_audioAllocCount so GetDiagnostics can prove the mix is allocation-free.
//
// Only the plain (non-aligned) new/new[] forms are replaced — over-aligned allocations keep
// the default operator new(align_val_t)/delete(align_val_t), which pair among themselves, so
// there is no malloc/free-vs-aligned mismatch. glibc free() accepts pointers from malloc and
// from the default aligned allocators alike, so every delete form here can just free().

#include "AudioAllocGuard.h"

#include <cstdlib>
#include <new>

namespace gloopy
{
    thread_local bool     g_onAudioThread   = false;
    std::atomic<long long> g_audioAllocCount { 0 };
}

static inline void* gloopyAlloc (std::size_t n)
{
    if (gloopy::g_onAudioThread)
        gloopy::g_audioAllocCount.fetch_add (1, std::memory_order_relaxed);
    if (n == 0) n = 1;                    // new(0) must return a unique non-null pointer
    return std::malloc (n);
}

void* operator new (std::size_t n)
{
    if (void* p = gloopyAlloc (n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (void* p = gloopyAlloc (n)) return p;
    throw std::bad_alloc();
}
void* operator new   (std::size_t n, const std::nothrow_t&) noexcept { return gloopyAlloc (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return gloopyAlloc (n); }

void operator delete   (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete   (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete   (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
