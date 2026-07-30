// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "KernelHost.h"

#include <grpcpp/grpcpp.h>
#include "gloopy.grpc.pb.h"
#include <cstdlib>   // setenv

namespace gpb = gloopy::v1;

struct KernelHost::Impl
{
    juce::ChildProcess proc;
    int port { 0 };
    std::shared_ptr<grpc::Channel>      channel;
    std::unique_ptr<gpb::Kernel::Stub>  stub;

    // Locate common-lisp/kernel.lisp: $GLOOPY_KERNEL, else relative to the CWD or
    // the executable (walking up a few levels for a dev/source-tree layout).
    static juce::File findKernel()
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

    bool start (juce::String& error)
    {
        if (stub != nullptr) return true;

        const auto kernel = findKernel();
        if (! kernel.existsAsFile()) { error = "kernel: cannot find common-lisp/kernel.lisp"; return false; }

        // The kernel writes its chosen port to this file once it's ready (avoids parsing
        // stdout past the slow first-launch proto-compile output).
        const auto portFile = juce::File::createTempFile ("gloopy-kernel-port");
        portFile.deleteFile();

        // Hand the port-file path to the child via the environment (ChildProcess inherits
        // the parent env at launch). --non-interactive --load (NOT --script: that skips
        // ~/.sbclrc, where ocicl registers ag-grpc).
        ::setenv ("GLOOPY_KERNEL_PORTFILE", portFile.getFullPathName().toRawUTF8(), 1);
        juce::StringArray argv { "sbcl", "--non-interactive", "--load", kernel.getFullPathName() };
        if (! proc.start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        { error = "kernel: failed to launch sbcl (is SBCL installed?)"; return false; }

        // Poll the port file (first launch compiles the proto — allow ~3 min).
        const auto deadline = juce::Time::getMillisecondCounter() + 180000;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (portFile.existsAsFile())
            {
                port = portFile.loadFileAsString().trim().getIntValue();
                if (port > 0) break;
            }
            if (! proc.isRunning())
            { error = "kernel: sbcl exited before reporting a port"; portFile.deleteFile(); return false; }
            juce::Thread::sleep (200);
        }
        portFile.deleteFile();
        if (port <= 0) { error = "kernel: timed out waiting for the kernel to start"; return false; }

        channel = grpc::CreateChannel ("127.0.0.1:" + std::to_string (port), grpc::InsecureChannelCredentials());
        stub = gpb::Kernel::NewStub (channel);
        return true;
    }
};

KernelHost::KernelHost() : impl (std::make_unique<Impl>()) {}
KernelHost::~KernelHost() { shutdown(); }

bool KernelHost::ensureStarted (juce::String& error) { return impl->start (error); }

bool KernelHost::generate (const GenParams& p, std::vector<Note>& out, juce::String& error)
{
    if (! impl->start (error)) return false;

    // If a source file is given, (re)load it into the warm image first.
    if (p.source.isNotEmpty())
    {
        gpb::LoadRequest lr; lr.set_path (p.source.toStdString());
        gpb::LoadResult  lres; grpc::ClientContext lctx;
        const auto ls = impl->stub->LoadSource (&lctx, lr, &lres);
        if (! ls.ok())      { error = "kernel LoadSource: " + juce::String (ls.error_message()); return false; }
        if (! lres.ok() && lres.diagnostics_size() > 0)
        { error = "kernel LoadSource: " + juce::String (lres.diagnostics (0).message()); return false; }
    }

    gpb::GenRequest req;
    if (p.entry.isNotEmpty()) req.set_entry (p.entry.toStdString());
    auto* ctx = req.mutable_context();
    ctx->set_tempo_bpm (p.tempoBpm);
    ctx->set_clip_len_beats (p.clipLenBeats);
    ctx->set_key_root (p.keyRoot);
    ctx->set_seed ((google::protobuf::uint64) p.seed);
    ctx->set_track_id (p.trackId);
    ctx->set_clip_index (p.clipIndex);

    gpb::GenResult res; grpc::ClientContext cctx;
    const auto st = impl->stub->Generate (&cctx, req, &res);
    if (! st.ok()) { error = "kernel Generate: " + juce::String (st.error_message()); return false; }
    if (! res.ok())
    { error = res.diagnostics_size() > 0 ? juce::String (res.diagnostics (0).message()) : "kernel Generate failed"; return false; }

    out.clear();
    for (const auto& n : res.notes())
    {
        Note note;
        note.pitch       = n.pitch();
        note.startBeat   = n.start_beat();
        note.lengthBeats = n.length_beats();
        note.velocity    = n.velocity();
        if (n.probability() > 0.0f) note.probability = n.probability();
        out.push_back (note);
    }
    return true;
}

void KernelHost::shutdown()
{
    impl->stub.reset();
    impl->channel.reset();
    if (impl->proc.isRunning()) { impl->proc.kill(); }
}
