# Python client

The `gloopy` package wraps the [gRPC API](../grpc/index.md) in idiomatic Python.
Install with `pip install ./python`. For a guided start, do the
[Python quickstart](../../tutorials/python-quickstart.md).

```python
from gloopy import Gloopy, note

with Gloopy() as g:                 # connects to 127.0.0.1:50051
    lead = g.add_synth_track("Lead", wave="SAW")
    g.add_clip(lead, notes=[note(60, 0, 1), note(64, 1, 1), note(67, 2, 1)])
    g.play()
    g.render("/tmp/mix.wav", tail_seconds=1.0)
```

Methods map roughly one-to-one onto the RPCs and return plain Python values.
`note(pitch, start_beat, length_beats, velocity=0.8)` is a small helper for
building notes.

!!! info "Pulled live from the source"
    The reference below is generated at build time from the docstrings in
    `python/gloopy/client.py` (mkdocstrings, Google-style docstrings) — no
    pre-generation step, so it never drifts from the code. If it's blank here,
    the docs environment doesn't have the `gloopy` package importable yet.

::: gloopy.client
    options:
      show_source: false
      members_order: source
      show_root_heading: true
      heading_level: 2
