# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Gloopy — Python client for the gRPC control API.

See ``client.py`` for the ``Gloopy`` class. The ``gloopy_pb2*`` modules are
generated from ``proto/gloopy.proto`` (regenerate with ``python/gen.sh``).
"""
from .client import Gloopy, connect, note, WAVEFORMS, EFFECTS, AUTO_TARGETS

__all__ = ["Gloopy", "connect", "note", "WAVEFORMS", "EFFECTS", "AUTO_TARGETS"]
