// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "KernelHost.h"
#include <cstdlib>   // setenv

juce::File KernelHost::findKernel()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("GLOOPY_KERNEL", {}); env.isNotEmpty())
        if (juce::File f (env); f.existsAsFile()) return f;
    juce::Array<juce::File> roots;
    roots.add (juce::File::getCurrentWorkingDirectory());
    roots.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory());
    for (auto base : roots)
        for (int up = 0; up < 6 && base != juce::File(); ++up, base = base.getParentDirectory())
            if (auto f = base.getChildFile ("common-lisp/kernel.lisp"); f.existsAsFile())
                return f;
    return {};
}

std::unique_ptr<juce::ChildProcess>
KernelHost::launchGenerate (const juce::String& job, const GenParams& p, int hostPort, juce::String& error)
{
    const auto kernel = findKernel();
    if (! kernel.existsAsFile()) { error = "kernel: cannot find common-lisp/kernel.lisp"; return {}; }

    // Hand the job + context to the child via the environment (ChildProcess inherits it).
    // GLOOPY_JOB puts kernel.lisp into "submit mode": generate, then POST the notes to
    // Gloopy's KernelSubmit RPC as an ag-grpc client (the interop-proven direction).
    auto set = [] (const char* k, const juce::String& v) { ::setenv (k, v.toRawUTF8(), 1); };
    set ("GLOOPY_JOB",       job);
    set ("GLOOPY_HOST_PORT", juce::String (hostPort));
    set ("GLOOPY_CTX_TEMPO", juce::String (p.tempoBpm));
    set ("GLOOPY_CTX_LEN",   juce::String (p.clipLenBeats));
    set ("GLOOPY_CTX_SEED",  juce::String (p.seed));
    set ("GLOOPY_CTX_KEY",   juce::String (p.keyRoot));
    set ("GLOOPY_SOURCE",    p.source);

    // --non-interactive --load (NOT --script: that skips ~/.sbclrc, where ocicl registers ag-grpc).
    juce::StringArray argv { "sbcl", "--non-interactive", "--load", kernel.getFullPathName() };
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { error = "kernel: failed to launch sbcl (is SBCL installed?)"; return {}; }
    return proc;
}
