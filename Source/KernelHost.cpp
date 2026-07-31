// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "KernelHost.h"
#include <cstdlib>   // setenv

juce::File KernelHost::findFile (const juce::String& relPath)
{
    juce::Array<juce::File> roots;
    roots.add (juce::File::getCurrentWorkingDirectory());
    roots.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory());
    for (auto base : roots)
        for (int up = 0; up < 6 && base != juce::File(); ++up, base = base.getParentDirectory())
            if (auto f = base.getChildFile (relPath); f.existsAsFile())
                return f;
    return {};
}

std::unique_ptr<juce::ChildProcess>
KernelHost::launchGenerate (const juce::String& job, const GenParams& p, int hostPort, juce::String& error)
{
    const bool python = p.lang.equalsIgnoreCase ("python");
    const auto rel = python ? juce::String ("python/kernel.py") : juce::String ("common-lisp/kernel.lisp");
    const auto kernel = findFile (rel);
    if (! kernel.existsAsFile()) { error = "kernel: cannot find " + rel; return {}; }

    // Hand the job + context to the child via the environment (ChildProcess inherits it).
    // GLOOPY_JOB puts kernel.lisp into "submit mode": generate, then POST the notes to
    // Gloopy's KernelSubmit RPC as an ag-grpc client (the interop-proven direction).
    auto set = [] (const char* k, const juce::String& v) { ::setenv (k, v.toRawUTF8(), 1); };
    ::unsetenv ("GLOOPY_SWANK");                        // don't inherit REPL mode into a generate
    set ("GLOOPY_JOB",       job);
    set ("GLOOPY_HOST_PORT", juce::String (hostPort));
    set ("GLOOPY_CTX_TEMPO", juce::String (p.tempoBpm));
    set ("GLOOPY_CTX_LEN",   juce::String (p.clipLenBeats));
    set ("GLOOPY_CTX_SEED",  juce::String (p.seed));
    set ("GLOOPY_CTX_KEY",   juce::String (p.keyRoot));
    set ("GLOOPY_SOURCE",    p.source);

    // Python: `python3 kernel.py`. Lisp: --non-interactive --load (NOT --script, which
    // skips ~/.sbclrc where ocicl registers ag-grpc).
    juce::StringArray argv = python
        ? juce::StringArray { "python3", kernel.getFullPathName() }
        : juce::StringArray { "sbcl", "--non-interactive", "--load", kernel.getFullPathName() };
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { error = "kernel: failed to launch sbcl (is SBCL installed?)"; return {}; }
    return proc;
}

std::unique_ptr<juce::ChildProcess>
KernelHost::launchServe (int hostPort, juce::String& error)
{
    const auto kernel = findFile ("common-lisp/kernel.lisp");
    if (! kernel.existsAsFile()) { error = "kernel: cannot find common-lisp/kernel.lisp"; return {}; }

    ::unsetenv ("GLOOPY_JOB"); ::unsetenv ("GLOOPY_SWANK");
    ::setenv ("GLOOPY_SERVE", "1", 1);
    ::setenv ("GLOOPY_HOST_PORT", juce::String (hostPort).toRawUTF8(), 1);

    juce::StringArray argv { "sbcl", "--non-interactive", "--load", kernel.getFullPathName() };
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { error = "kernel: failed to launch sbcl (is SBCL installed?)"; return {}; }
    return proc;
}

std::unique_ptr<juce::ChildProcess>
KernelHost::launchRepl (int& swankPortOut, juce::String& error)
{
    const auto kernel = findFile ("common-lisp/kernel.lisp");
    if (! kernel.existsAsFile()) { error = "kernel: cannot find common-lisp/kernel.lisp"; return {}; }

    const auto portFile = juce::File::createTempFile ("gloopy-swank-port");
    portFile.deleteFile();
    ::unsetenv ("GLOOPY_JOB");                          // REPL mode, not a one-shot generate
    ::setenv ("GLOOPY_SWANK", "1", 1);
    ::setenv ("GLOOPY_KERNEL_PORTFILE", portFile.getFullPathName().toRawUTF8(), 1);

    juce::StringArray argv { "sbcl", "--non-interactive", "--load", kernel.getFullPathName() };
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { error = "kernel: failed to launch sbcl (is SBCL installed?)"; return {}; }

    const auto deadline = juce::Time::getMillisecondCounter() + 180000;   // compile + load swank
    int port = 0;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (portFile.existsAsFile()) { port = portFile.loadFileAsString().trim().getIntValue(); if (port > 0) break; }
        if (! proc->isRunning()) { error = "kernel: sbcl exited before starting SWANK"; portFile.deleteFile(); return {}; }
        juce::Thread::sleep (200);
    }
    portFile.deleteFile();
    if (port <= 0) { proc->kill(); error = "kernel: timed out starting SWANK"; return {}; }
    swankPortOut = port;
    return proc;
}
