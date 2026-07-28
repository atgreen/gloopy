# How-to guides

Task recipes for driving Gloopy from code or hardware. Each assumes you've read
[The Gloopy model](../concepts/model.md) and done one of the quickstarts
([Python](../tutorials/python-quickstart.md) or
[Common Lisp](../tutorials/lisp-quickstart.md)).

## Available now

- **[Let an AI agent drive Gloopy (MCP)](mcp-server.md)** — expose the control
  API to an assistant over the Model Context Protocol.
- **[Subscribe to the playhead and meters](subscribe-playhead-meters.md)** — a
  live push feed of transport position and levels, in Python, Lisp, and raw gRPC.

!!! note "Being written"
    Planned guides:

    - Set up a MIDI control surface over OSC
    - Sync transport with an external clock
    - Bulk-import a generated MIDI sequence (`ImportNotesJSON`)
    - Bind a hardware knob to any parameter (MIDI-learn)
    - Render named export profiles from a script or CI

    Until these land, the [gRPC](../reference/grpc/index.md) and
    [OSC](../reference/osc/index.md) references list every available operation.
