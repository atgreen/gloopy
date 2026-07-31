// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "KernelHost.h"
#include <cstdlib>   // setenv / _putenv_s

namespace
{
    // Portable env helpers: MSVC has no POSIX setenv/unsetenv (it uses _putenv_s).
    void khSetEnv (const char* name, const juce::String& value)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());
       #else
        ::setenv (name, value.toRawUTF8(), 1);
       #endif
    }
    void khUnsetEnv (const char* name)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, "");     // empty value removes it from the environment
       #else
        ::unsetenv (name);
       #endif
    }
}

juce::File KernelHost::findFile (const juce::String& relPath)
{
    juce::Array<juce::File> roots;
    roots.add (juce::File::getCurrentWorkingDirectory());
    roots.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory());
    for (auto base : roots)
        for (int up = 0; up < 6 && base != juce::File(); ++up, base = base.getParentDirectory())
        {
            if (auto f = base.getChildFile (relPath); f.existsAsFile())
                return f;                                            // dev tree (repo root / build dir)
            if (auto f = base.getChildFile ("share/gloopy").getChildFile (relPath); f.existsAsFile())
                return f;                                            // installed: <prefix>/share/gloopy/...
        }
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
    auto set = [] (const char* k, const juce::String& v) { khSetEnv (k, v); };
    khUnsetEnv ("GLOOPY_SWANK");                        // don't inherit REPL mode into a generate
    if (auto proto = findFile ("proto/gloopy.proto"); proto.existsAsFile())
        set ("GLOOPY_PROTO", proto.getFullPathName());   // so the kernel finds it when installed
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

    khUnsetEnv ("GLOOPY_JOB"); khUnsetEnv ("GLOOPY_SWANK");
    khSetEnv ("GLOOPY_SERVE", "1");
    khSetEnv ("GLOOPY_HOST_PORT", juce::String (hostPort));
    if (auto proto = findFile ("proto/gloopy.proto"); proto.existsAsFile())
        khSetEnv ("GLOOPY_PROTO", proto.getFullPathName());   // so the kernel finds it when installed

    juce::StringArray argv { "sbcl", "--non-interactive", "--load", kernel.getFullPathName() };
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { error = "kernel: failed to launch sbcl (is SBCL installed?)"; return {}; }
    return proc;
}
