# Control & scripting

For integrators, script authors, and hardware vendors. Gloopy exposes everything
it can do over a **control API** so external programs can build, edit, perform,
and render compositions.

## Read this first

Everything here assumes the shared vocabulary and, especially, the
**two-lane** control model:

[:material-cube-outline: The Gloopy model](concepts/model.md){ .md-button .md-button--primary }

The short version: **gRPC** is the structural, scheduled lane (build and edit the
composition, query state, render); **OSC** is the live, low-latency lane (play
notes, turn knobs, start/stop). gRPC creates the stable `id`s; OSC performs on
them. See [the two control lanes](concepts/model.md#the-two-control-lanes).

## Get moving

[:material-language-python: Drive Gloopy from Python](tutorials/python-quickstart.md){ .md-button }
[:material-code-parentheses: Drive Gloopy from Common Lisp](tutorials/lisp-quickstart.md){ .md-button }

## This section

- **[Concepts](concepts/model.md)** — the domain model and the two control lanes,
  defined once, language-agnostic. Every reference page links here.
- **[Tutorials](tutorials/python-quickstart.md)** — end-to-end lessons, in
  [Python](tutorials/python-quickstart.md) and
  [Common Lisp](tutorials/lisp-quickstart.md).
- **[How-to guides](how-to/index.md)** — task recipes: let an
  [AI agent drive Gloopy](how-to/mcp-server.md), or
  [subscribe to the playhead and meters](how-to/subscribe-playhead-meters.md).
- **Reference** — the interfaces themselves:
  [gRPC](reference/grpc/index.md) · [OSC](reference/osc/index.md) ·
  [Python](reference/python/index.md) · [Common Lisp](reference/lisp/index.md).
- **[Explanation](explanation/index.md)** — why two protocols instead of MIDI-only.

## Endpoints at a glance

| Lane | Endpoint | Transport | Use for |
|------|----------|-----------|---------|
| gRPC | `127.0.0.1:50051` | TCP, request/response + streams | structure, edits, queries, render |
| OSC  | `9000` | UDP, fire-and-forget | live notes, knob moves, transport |
