# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Live script-clip kernel — attach a notebook/REPL as Gloopy's Python generator source.

This is the Python analogue of connecting Emacs (Sly) to the Lisp kernel: instead of Gloopy
launching a kernel, *your* process (a Jupyter notebook, an IPython session) attaches to a
running Gloopy and becomes the live source for its Python script clips. Redefine a generator
in a cell and the next generate — or the next pass of a "Live" clip — runs the new version.

    import gloopy
    k = gloopy.attach()                 # background thread now long-polls Gloopy

    @k.generator                        # register (as a decorator or k.set_generator(fn))
    def gen(ctx):
        return [gloopy.note(60 + b % 12, b, 0.9) for b in range(int(ctx.clip_len_beats))]

Mark a Python script clip "Live" (or call RegenerateClip with lang="python") and Gloopy hands
the job to this process: it builds the context, calls your generator, and posts the notes back.
Jobs are routed by language, so this coexists with Gloopy's own SBCL kernel.
"""
from __future__ import annotations

import threading
import time
import traceback
from typing import Callable, List

import grpc

from . import gloopy_pb2 as pb
from . import gloopy_pb2_grpc as rpc
from .client import note

Generator = Callable[["pb.GenContext"], List["pb.Note"]]


def default_generate(ctx) -> List["pb.Note"]:
    """Ascending diatonic run, one note per beat — used until you register a generator."""
    beats = max(1, int(ctx.clip_len_beats))
    root = 60 + (ctx.key_root if ctx.key_root >= 0 else 0)
    scale = [0, 2, 4, 5, 7, 9, 11]
    return [note(root + scale[b % len(scale)], b, 0.9) for b in range(beats)]


class LiveKernel:
    """A background bridge that serves Python generate jobs from the current process.

    Runs a daemon thread that long-polls ``KernelPoll(lang="python")``; on a job it calls the
    registered generator and submits the notes. Exceptions in your generator are caught and
    reported back to Gloopy (the clip keeps its last good notes), never killing the bridge.
    """

    def __init__(self, target: str = "127.0.0.1:50051"):
        self.target = target
        self._chan = grpc.insecure_channel(target)
        self._stub = rpc.GloopyStub(self._chan)
        self._default: Generator | None = None       # the unnamed generator
        self._generators: dict[str, Generator] = {}   # named generators (clip references them by name)
        self._stop = threading.Event()
        self._last_error: str | None = None
        self._thread = threading.Thread(target=self._serve, name="gloopy-live-kernel", daemon=True)
        self._thread.start()

    # -- registration ------------------------------------------------------
    def generator(self, fn_or_name=None):
        """Register a generator ``(ctx) -> list of notes``. Usable as a decorator:

            @k.generator            # the unnamed/default generator
            @k.generator("bass")    # a named generator a clip references by name
        """
        if callable(fn_or_name):                 # @k.generator
            self._default = fn_or_name
            return fn_or_name
        name = fn_or_name                         # @k.generator("bass")

        def deco(fn: Generator) -> Generator:
            if name:
                self._generators[name] = fn
            else:
                self._default = fn
            return fn
        return deco

    set_generator = generator  # alias for symmetry with the Lisp/one-shot kernels

    # -- the bridge --------------------------------------------------------
    def _run_job(self, spec) -> None:
        ctx = pb.GenContext(
            tempo_bpm=spec.tempo_bpm,
            clip_len_beats=spec.clip_len_beats,
            seed=spec.seed,
            key_root=spec.key_root,
        )
        try:
            name = spec.generator
            gen = (self._generators.get(name) if name else None) or self._default or default_generate
            notes = list(gen(ctx))
            self._stub.KernelSubmit(pb.KernelSubmitRequest(job=spec.job, ok=True, notes=notes))
            self._last_error = None
        except Exception as e:  # noqa: BLE001 — surface any generator error to Gloopy, keep serving
            self._last_error = f"{type(e).__name__}: {e}"
            traceback.print_exc()
            self._stub.KernelSubmit(pb.KernelSubmitRequest(job=spec.job, ok=False, error=self._last_error))

    def _serve(self) -> None:
        while not self._stop.is_set():
            try:
                spec = self._stub.KernelPoll(pb.KernelPollRequest(lang="python"), timeout=20)
            except grpc.RpcError:
                time.sleep(0.5)  # Gloopy not up yet / transient — back off and re-poll
                continue
            if spec.job:
                self._run_job(spec)
            else:
                time.sleep(0.05)  # empty poll (server-side timeout) — re-poll promptly

    # -- lifecycle ---------------------------------------------------------
    def detach(self) -> None:
        """Stop serving and close the channel. Gloopy falls back to a clip's cached notes."""
        self._stop.set()
        self._chan.close()

    @property
    def last_error(self) -> str | None:
        """The most recent generator error reported to Gloopy, if any (for notebook feedback)."""
        return self._last_error

    def __repr__(self) -> str:
        state = "registered" if self._gen else "default generator"
        return f"<gloopy.LiveKernel {self.target} — {state}>"


def attach(target: str = "127.0.0.1:50051") -> LiveKernel:
    """Attach this process as Gloopy's live Python generator kernel. Returns a :class:`LiveKernel`.

    The bridge starts immediately on a background daemon thread; register a generator with the
    returned object's ``@k.generator`` decorator (or ``k.set_generator``).
    """
    return LiveKernel(target)
