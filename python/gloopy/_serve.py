# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Headless Gloopy Python kernel — attach as the live generator source and serve jobs.

Gloopy launches this (KernelHost::launchServePython) so Python script clips generate without a
notebook, the way it auto-launches SBCL for Lisp. Two modes:

* **ipykernel available + GLOOPY_PY_CONNFILE set** — run as a Jupyter kernel bound to that
  connection file *and* ``gloopy.attach()`` in the same process. "Open Python notebook" then
  attaches a frontend to this exact kernel: cells run in the process that serves Gloopy, so a
  redefined generator is live on the next generate. This is the attach-to-live model — the
  Python twin of Emacs attaching to the one SBCL image over Slynk.

* **otherwise** — a plain blocking ``attach()`` loop. Serving still works headlessly; only the
  live notebook attach is unavailable (needs ``pip install ipykernel``).

Config via the environment (Gloopy sets these): ``GLOOPY_HOST_PORT`` (Gloopy's gRPC port,
default 50051), ``GLOOPY_PY_CONNFILE`` (Jupyter connection-file path, optional).
"""
from __future__ import annotations

import os
import sys
import time


def _target() -> str:
    return "127.0.0.1:" + os.environ.get("GLOOPY_HOST_PORT", "50051")


def _die_with_parent() -> None:
    """Linux: ask the kernel to signal us if Gloopy (our parent) dies, so we never outlive it as
    an orphan spinning on a gRPC server that's gone. Best-effort; a no-op off Linux."""
    if sys.platform != "linux":
        return
    try:
        import ctypes
        import signal

        PR_SET_PDEATHSIG = 1
        ctypes.CDLL("libc.so.6", use_errno=True).prctl(PR_SET_PDEATHSIG, signal.SIGTERM)
    except Exception:  # noqa: BLE001 — a missing prctl must not stop the kernel from serving
        pass


def _serve_ipykernel(connfile: str, target: str) -> bool:
    """Run as a Jupyter kernel bound to `connfile`, also serving Gloopy jobs. Returns False
    (so the caller falls back to the plain loop) if ipykernel isn't available or fails to start."""
    try:
        from ipykernel.kernelapp import IPKernelApp

        import gloopy

        app = IPKernelApp.instance()
        app.initialize(["-f", connfile])   # bind to the connection file Gloopy chose
        gloopy.attach(target)              # same process serves jobs; notebook edits are live
        print(f"[py-kernel] ipykernel on {connfile}, serving {target}", flush=True)
        app.start()                        # blocks until the kernel shuts down
        return True
    except Exception as e:  # noqa: BLE001 — any ipykernel problem must not break headless serving
        print(f"[py-kernel] ipykernel launch failed ({e}); serving headless instead", flush=True)
        return False


def main() -> None:
    _die_with_parent()
    target = _target()
    connfile = os.environ.get("GLOOPY_PY_CONNFILE", "")

    if connfile and _serve_ipykernel(connfile, target):
        return

    # Fallback: headless serve loop (no notebook attach). The daemon poll thread does the work.
    import gloopy

    gloopy.attach(target)
    print(f"[py-kernel] serving {target} (headless; no ipykernel, notebook attach unavailable)",
          flush=True)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
