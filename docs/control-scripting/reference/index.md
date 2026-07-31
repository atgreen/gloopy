# Reference

Dry, complete descriptions of each control interface. For the concepts behind
them, see [The Gloopy model](../concepts/model.md); for worked lessons, the
[tutorials](../tutorials/python-quickstart.md).

| Interface | What it is | Source of truth | How it's documented |
|-----------|------------|-----------------|---------------------|
| **[gRPC API](grpc/index.md)** | The structural/scheduled service (~140 RPCs) | `proto/gloopy.proto` | Hand-maintained summary; the proto is authoritative (`buf` generation planned) |
| **[OSC address space](osc/index.md)** | The live/performance lane | `Source/OscControl.h` | Hand-maintained summary; the source is authoritative (schema generation planned) |
| **[Python client](python/index.md)** | The `gloopy` package | `python/gloopy/client.py` docstrings | Pulled live at build (mkdocstrings) |
| **[Common Lisp client](lisp/index.md)** | The `gloopy` + `gloopy.osc` systems | `common-lisp/src/packages.lisp` exports | **Hand-maintained** (generator planned) |

!!! info "How these pages are produced"
    Only the **Python** reference is generated — pulled live from the client's
    docstrings by `mkdocstrings` at build time (don't edit it; change the
    docstring). The **gRPC**, **OSC**, and **Common Lisp** pages are
    **hand-maintained summaries** for now: the
    [proto](https://github.com/atgreen/gloopy/blob/main/proto/gloopy.proto),
    `Source/OscControl.h`, and the CL package exports remain the authoritative
    source. The `buf`, OSC-schema, and 40ants-doc generators are planned (their
    steps are stubbed in `.github/workflows/docs.yml`) and will replace the
    summaries when wired.
