// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "Note.h"

/** Launches and drives a language kernel over the gloopy.v1.Kernel gRPC service
    (the SBCL reference kernel is common-lisp/kernel.lisp). Gloopy is the client:
    it lazily starts the kernel process, keeps it warm, and calls Generate to turn
    a clip's context into notes. grpc/proto types are kept out of this header. */
class KernelHost
{
public:
    KernelHost();
    ~KernelHost();

    struct GenParams
    {
        double      tempoBpm     { 120.0 };
        double      clipLenBeats { 4.0 };
        int         keyRoot      { -1 };     // 0..11, or -1 = unset
        juce::int64 seed         { 0 };
        int         trackId      { 0 };
        int         clipIndex    { 0 };
        juce::String entry;                  // generator symbol (optional)
        juce::String source;                 // source file to LoadSource first (optional)
    };

    /** Start the kernel if it isn't running (compiles the proto on first launch —
        can take tens of seconds). Returns false and sets `error` on failure. */
    bool ensureStarted (juce::String& error);

    /** Generate notes for a clip context. Starts the kernel if needed. */
    bool generate (const GenParams& p, std::vector<Note>& out, juce::String& error);

    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KernelHost)
};
