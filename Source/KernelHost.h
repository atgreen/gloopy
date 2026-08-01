// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>

/** Launches a language kernel (the SBCL reference kernel, common-lisp/kernel.lisp) to
    generate a clip's notes. The kernel runs as an ag-grpc CLIENT of Gloopy's own gRPC
    service — it generates and posts the notes back via the KernelSubmit RPC (the proven
    direction; a grpc-c++ client into the ag-grpc server does not interop). This class only
    starts the process, wired up via the environment; results arrive on the KernelSubmit
    handler, correlated by the job id. */
class KernelHost
{
public:
    struct GenParams
    {
        double      tempoBpm     { 120.0 };
        double      clipLenBeats { 4.0 };
        int         keyRoot      { -1 };     // 0..11, or -1 = unset
        juce::int64 seed         { 0 };
        int         trackId      { 0 };
        int         clipIndex    { 0 };
        juce::String lang;                   // "common-lisp" (default) or "python"
        juce::String source;                 // source file to load before generating (optional)
        juce::String generator;              // OR a named generator ("pkg.mod:fn" / "pkg:sym")
        juce::String system;                 // ASDF system to load for a named Lisp generator (optional)
    };

    /** Start the kernel for `job`, generating with `p` and posting results back to
        Gloopy on 127.0.0.1:hostPort. The returned process must be kept alive until the
        job completes (the caller waits for KernelSubmit). Null on launch failure. */
    static std::unique_ptr<juce::ChildProcess>
        launchGenerate (const juce::String& job, const GenParams& p, int hostPort, juce::String& error);

    /** Start the persistent "warm" SBCL kernel in serve mode: it long-polls Gloopy for
        generate jobs (so the proto compiles once and every generate after is instant) and
        hosts a Slynk server for interactive development (cave #15), reporting its port back
        via KernelReady. Returns the running process (keep it alive); null on launch failure. */
    static std::unique_ptr<juce::ChildProcess> launchServe (int hostPort, juce::String& error);

    static juce::File findFile (const juce::String& relPath);   // locate a repo file (dev tree)
};
