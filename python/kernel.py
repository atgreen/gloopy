#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Python reference kernel for Gloopy script clips (cave #14).

Mirrors common-lisp/kernel.lisp: Gloopy launches this with a clip context in the
environment (submit mode); we generate the notes and POST them back to Gloopy's
KernelSubmit RPC as a gRPC client (the interop-proven direction). A user script
(GLOOPY_SOURCE) defines the generator via set_generator(...). Needs grpcio +
grpcio-tools; the proto is compiled to stubs at startup, like the Lisp kernel.

    python3 python/kernel.py        # env drives it (GLOOPY_JOB, GLOOPY_CTX_*, ...)
"""
import os, sys, tempfile, subprocess, random
import grpc


def _load_stubs():
    here = os.path.dirname(os.path.abspath(__file__))
    proto = os.environ.get("GLOOPY_PROTO") or os.path.join(here, "..", "proto", "gloopy.proto")
    proto = os.path.abspath(proto)
    out = tempfile.mkdtemp(prefix="gloopy-pyk-")
    subprocess.check_call([sys.executable, "-m", "grpc_tools.protoc",
                           "-I", os.path.dirname(proto),
                           "--python_out", out, "--grpc_python_out", out, proto])
    sys.path.insert(0, out)
    import gloopy_pb2 as pb            # noqa: E402
    import gloopy_pb2_grpc as pbg      # noqa: E402
    return pb, pbg


pb, pbg = _load_stubs()

# --- the script prelude: what a user generator uses ---
_generator = None


def set_generator(fn):
    """Register the generator: a callable (ctx) -> list of notes."""
    global _generator
    _generator = fn


def note(pitch, start, length, velocity=0.8):
    """A note; start/length in beats within the clip, velocity 0..1."""
    return pb.Note(pitch=int(round(pitch)), start_beat=float(start),
                   length_beats=float(length), velocity=float(velocity))


def default_generate(ctx):
    """Ascending diatonic run, one note per beat, deterministic in the seed."""
    beats = max(1, int(ctx.clip_len_beats))
    root = 60 + (ctx.key_root if ctx.key_root >= 0 else 0)
    scale = [0, 2, 4, 5, 7, 9, 11]
    rng = random.Random(ctx.seed & 0xffffffff)
    return [note(root + scale[(b + rng.randrange(3)) % len(scale)], b, 0.9) for b in range(beats)]


def submit_job():
    job = os.environ.get("GLOOPY_JOB", "")
    port = int(os.environ.get("GLOOPY_HOST_PORT", "50051"))
    source = os.environ.get("GLOOPY_SOURCE", "")
    ctx = pb.GenContext(
        tempo_bpm=float(os.environ.get("GLOOPY_CTX_TEMPO", "120")),
        clip_len_beats=float(os.environ.get("GLOOPY_CTX_LEN", "4")),
        seed=int(os.environ.get("GLOOPY_CTX_SEED", "0")),
        key_root=int(os.environ.get("GLOOPY_CTX_KEY", "-1")))
    ok, err, notes = True, "", []
    try:
        if source and os.path.isfile(source):
            g = {"note": note, "set_generator": set_generator, "pb": pb, "__name__": "gloopy_script"}
            exec(compile(open(source).read(), source, "exec"), g)
        notes = (_generator or default_generate)(ctx)
    except Exception as e:  # noqa: BLE001 — surface any script error to Gloopy
        ok, err = False, str(e)
    with grpc.insecure_channel(f"127.0.0.1:{port}") as ch:
        pbg.GloopyStub(ch).KernelSubmit(
            pb.KernelSubmitRequest(job=job, ok=ok, notes=notes, error=err))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    if os.environ.get("GLOOPY_JOB"):
        submit_job()
    else:
        print("gloopy python kernel: set GLOOPY_JOB (submit mode); launched by Gloopy", file=sys.stderr)
