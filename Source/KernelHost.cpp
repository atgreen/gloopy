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

    // The shared per-user cache root for everything the kernels need: SBCL fasls
    // (<root>/common-lisp), ocicl's global system collection (<root>/ocicl), and the
    // Python venv (<root>/python). Pinning these here — off the user's global setup and
    // off the launch cwd — keeps the kernels self-contained and makes repeat launches
    // fast: deps are fetched/compiled once and reused.
    juce::File gloopyCacheDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile (".cache/gloopy");
        dir.createDirectory();
        return dir;
    }

    // A stable, writable working directory for kernel child processes. The desktop
    // launcher starts Gloopy in "/", which the user can't write to; the SBCL kernel
    // loads ocicl from ~/.sbclrc, which registers the *current directory* as a
    // source-registry — so in "/" it fails and the kernel dies before loading
    // ag-grpc/Slynk. Launch kernels here instead of inheriting Gloopy's cwd.
    juce::File kernelWorkingDir()
    {
        auto dir = gloopyCacheDir().getChildFile ("kernel");
        dir.createDirectory();
        return dir.isDirectory() ? dir
                                 : juce::File::getSpecialLocation (juce::File::tempDirectory);
    }

    // Run after ~/.sbclrc (which loads ocicl-runtime) but before the kernel file: flip
    // ocicl into global mode (the `-g`/`--global` behaviour) so deps resolve to and
    // install into <root>/ocicl regardless of cwd or any local ocicl.csv. Guarded, so a
    // plain SBCL with no ocicl just no-ops.
    const char* const kOciclForceGlobal =
        "(when (find-package :ocicl-runtime)"
        " (setf (symbol-value (find-symbol \"*FORCE-GLOBAL*\" :ocicl-runtime)) t))";

    // The SBCL kernel command. Pin ASDF fasls (XDG_CACHE_HOME -> <root>/common-lisp) and
    // ocicl's global system collection (XDG_DATA_HOME -> <root>/ocicl) via an `env` prefix
    // — scoped to the child, NOT set on Gloopy's own process — then force ocicl global mode
    // before loading the kernel. (~/.sbclrc loads ocicl-runtime by absolute path, so
    // redirecting XDG_DATA_HOME doesn't disturb the runtime load.) The env prefix is
    // POSIX-only; the cwd/ocicl issue it addresses is desktop-Linux/macOS-specific.
    juce::StringArray lispKernelArgv (const juce::File& kernel)
    {
        juce::StringArray a;
       #if ! JUCE_WINDOWS
        const auto root = gloopyCacheDir().getFullPathName();
        a.add ("env");
        a.add ("XDG_CACHE_HOME=" + root);
        a.add ("XDG_DATA_HOME=" + root);
       #endif
        a.add ("sbcl");
        a.add ("--non-interactive");
        a.add ("--eval");
        a.add (kOciclForceGlobal);
        a.add ("--load");
        a.add (kernel.getFullPathName());
        return a;
    }

    // The interpreter to run a Python kernel: GLOOPY_PYTHON if set, else python3/python.
    juce::String basePython()
    {
       #if JUCE_WINDOWS
        const juce::String dflt = "python";
       #else
        const juce::String dflt = "python3";
       #endif
        if (const char* e = std::getenv ("GLOOPY_PYTHON"); e != nullptr && e[0] != '\0')
            return juce::String (e);
        return dflt;
    }

    // A shared Python venv at <root>/python, created and provisioned (grpcio, protobuf,
    // ipykernel) on first use so the kernel doesn't depend on the system site-packages.
    // Returns the venv's interpreter; returns {} (caller falls back to the system python)
    // if the venv can't be built. Provisioning is one-time — marked by a ready file — but
    // that first `pip install` is slow and needs network, so callers run this off the
    // message thread.
    juce::File pythonVenvInterpreter (const juce::String& base)
    {
        const auto venv  = gloopyCacheDir().getChildFile ("python");
       #if JUCE_WINDOWS
        const auto py    = venv.getChildFile ("Scripts").getChildFile ("python.exe");
       #else
        const auto py    = venv.getChildFile ("bin").getChildFile ("python");
       #endif
        const auto ready = venv.getChildFile (".gloopy-ready");
        if (py.existsAsFile() && ready.existsAsFile())
            return py;

        auto run = [] (const juce::StringArray& argv) -> bool
        {
            juce::ChildProcess p;
            if (! p.start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
                return false;
            p.readAllProcessOutput();                     // blocks until the child exits
            return p.getExitCode() == 0;
        };

        if (! py.existsAsFile())
            if (! run ({ base, "-m", "venv", venv.getFullPathName() }))
                return {};                                // no venv module / creation failed
        if (! run ({ py.getFullPathName(), "-m", "pip", "install", "--disable-pip-version-check",
                     "-q", "grpcio", "protobuf", "ipykernel" }))
            return {};                                    // provisioning failed -> system python
        ready.create();
        return py;
    }

    // The Python interpreter for a kernel launch: the shared venv if it can be built,
    // otherwise the base (GLOOPY_PYTHON / system) interpreter.
    juce::String pythonForKernel()
    {
        const auto base = basePython();
        const auto venv = pythonVenvInterpreter (base);
        return venv.existsAsFile() ? venv.getFullPathName() : base;
    }

    // Wrap argv so the child runs in `dir`. JUCE's ChildProcess inherits the parent's
    // cwd and offers no override, so on POSIX we exec through `sh -c`, which cds first
    // then `exec`s the real command in the same pid (so JUCE still tracks/kills it).
    // Positional args ($1 = dir, then the command) sidestep hand-quoting the paths.
    juce::StringArray inDir (const juce::File& dir, const juce::StringArray& argv)
    {
       #if JUCE_WINDOWS
        juce::ignoreUnused (dir);
        return argv;                     // the cwd="/" landmine is POSIX/desktop-specific
       #else
        juce::StringArray wrapped;
        wrapped.add ("/bin/sh");
        wrapped.add ("-c");
        wrapped.add ("cd \"$1\" || exit 1; shift; exec \"$@\"");
        wrapped.add ("gloopy-kernel");   // $0
        wrapped.add (dir.getFullPathName());
        wrapped.addArray (argv);
        return wrapped;
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

    // Python: run kernel.py with the shared venv interpreter. Lisp: --non-interactive
    // --load (NOT --script, which skips ~/.sbclrc where ocicl is registered), with the
    // fasl/ocicl caches pinned to ~/.cache/gloopy and ocicl forced into global mode.
    juce::StringArray argv;
    if (python)
        argv = { pythonForKernel(), kernel.getFullPathName() };
    else
        argv = lispKernelArgv (kernel);
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (inDir (kernelWorkingDir(), argv), juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
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

    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (inDir (kernelWorkingDir(), lispKernelArgv (kernel)), juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { error = "kernel: failed to launch sbcl (is SBCL installed?)"; return {}; }
    return proc;
}

std::unique_ptr<juce::ChildProcess>
KernelHost::launchServePython (int hostPort, const juce::String& connFile, juce::String& error)
{
    // Locate the `gloopy` package so `-m gloopy._serve` imports in both the dev tree and an
    // installed build; its grandparent (the `python/` dir) is the import root for PYTHONPATH.
    const auto serve = findFile ("python/gloopy/_serve.py");
    if (! serve.existsAsFile()) { error = "kernel: cannot find python/gloopy/_serve.py"; return {}; }
    const auto pyRoot = serve.getParentDirectory().getParentDirectory();   // .../python

    khUnsetEnv ("GLOOPY_JOB"); khUnsetEnv ("GLOOPY_SWANK"); khUnsetEnv ("GLOOPY_SERVE");
    khSetEnv ("GLOOPY_HOST_PORT", juce::String (hostPort));
    khSetEnv ("GLOOPY_KERNEL_HEADLESS", "1");   // the fallback kernel: don't announce (a notebook preempts it)
    if (connFile.isNotEmpty()) khSetEnv ("GLOOPY_PY_CONNFILE", connFile);
    else                       khUnsetEnv ("GLOOPY_PY_CONNFILE");

   #if JUCE_WINDOWS
    const char pathSep = ';';
   #else
    const char pathSep = ':';
   #endif
    // Prepend our import root so the child finds the package (dev tree or installed).
    juce::String pythonPath = pyRoot.getFullPathName();
    if (const char* cur = std::getenv ("PYTHONPATH"); cur != nullptr && cur[0] != '\0')
        pythonPath << pathSep << cur;
    khSetEnv ("PYTHONPATH", pythonPath);

    // Run the kernel with the shared venv interpreter (grpcio/protobuf/ipykernel), created
    // under ~/.cache/gloopy/python on first use; falls back to GLOOPY_PYTHON / system.
    const juce::String python = pythonForKernel();

    juce::StringArray argv { python, "-m", "gloopy._serve" };
    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (inDir (kernelWorkingDir(), argv), juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { error = "kernel: failed to launch " + python + " (is Python on PATH?)"; return {}; }
    return proc;
}
