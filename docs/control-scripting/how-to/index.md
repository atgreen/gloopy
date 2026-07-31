# How-to guides

Task recipes for driving Gloopy from code or hardware. Each assumes you've read
[The Gloopy model](../concepts/model.md) and done one of the quickstarts
([Python](../tutorials/python-quickstart.md) or
[Common Lisp](../tutorials/lisp-quickstart.md)).

## Available now

- **[Let an AI agent drive Gloopy (MCP)](mcp-server.md)** — expose the control
  API to an assistant over the Model Context Protocol.
- **[Write a script-clip generator](write-a-script-clip.md)** — generate a clip's
  notes from Common Lisp or Python code, materialised or live-driven.
- **[Subscribe to the playhead and meters](subscribe-playhead-meters.md)** — a
  live push feed of transport position and levels, in Python, Lisp, and raw gRPC.
- **[Add and control effects from a script](effects-from-a-script.md)** — put an
  effect on a track, set its parameters, bypass it, or host a plugin — Python,
  Lisp, and grpcurl.
- **[Play Gloopy live over OSC](play-live-over-osc.md)** — fire notes, sweep
  knobs, and drive the transport in real time from Common Lisp or any OSC sender.
- **[Bulk-import a generated MIDI sequence](import-generated-notes.md)** — hand
  Gloopy a whole part as JSON in one call.
- **[Bind a hardware knob to any parameter (MIDI-learn)](bind-a-controller.md)** —
  map a CC or OSC source to any parameter, scaled and invertible.
- **[Render export profiles from a script or CI](export-profiles-ci.md)** —
  named, repeatable bounces (mix / range / track / stems).

!!! note "Being written"
    Still planned: syncing the transport to an external clock — Gloopy can be
    *driven* over OSC today (see the live-OSC guide), but following an incoming
    MIDI clock isn't implemented yet.

    Until it lands, the [gRPC](../reference/grpc/index.md) and
    [OSC](../reference/osc/index.md) references list every available operation.
