# Reference

Dry, complete descriptions of each control interface. For the concepts behind
them, see [The Gloopy model](../concepts/model.md); for worked lessons, the
[tutorials](../tutorials/python-quickstart.md).

| Interface | What it is | Source of truth | How it's documented |
|-----------|------------|-----------------|---------------------|
| **[gRPC API](grpc/index.md)** | The structural/scheduled service (~140 RPCs) | `proto/gloopy.proto` | Generated from proto comments (`buf` + `protoc-gen-doc`) |
| **[OSC address space](osc/index.md)** | The live/performance lane | `Source/OscControl.h` → an `osc.yaml` schema | Generated from the schema |
| **[Python client](python/index.md)** | The `gloopy` package | `python/gloopy/client.py` docstrings | Pulled live at build (mkdocstrings) |
| **[Common Lisp client](lisp/index.md)** | The `gloopy` + `gloopy.osc` systems | `common-lisp/src/packages.lisp` exports | **Hand-maintained** (generator planned) |

!!! info "Generated pages"
    The gRPC, OSC, and Python reference bodies are **generated from the source of
    truth**, not written by hand. Never edit a generated page — change the proto
    comment, the schema, or the docstring it came from. Generated Markdown is
    git-ignored and rebuilt in CI on every deploy. The **Common Lisp** page is
    the current exception: it's written by hand from the package exports until the
    40ants-doc generator is wired up.
