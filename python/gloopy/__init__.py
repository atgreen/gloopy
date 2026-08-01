# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""Gloopy — Python client for the gRPC control API.

See ``client.py`` for the ``Gloopy`` class. The ``gloopy_pb2*`` modules are
generated from ``proto/gloopy.proto`` (regenerate with ``python/gen.sh``).
"""
from .client import Gloopy, connect, note, WAVEFORMS, EFFECTS, AUTO_TARGETS
from .live import attach, LiveKernel, generator, set_generator
from .music import (pitch, pitch_name, dur, scale, chord, seq, mini, REST,
                    SCALES, CHORDS)

__all__ = ["Gloopy", "connect", "note", "WAVEFORMS", "EFFECTS", "AUTO_TARGETS",
           "attach", "LiveKernel", "generator", "set_generator",
           "pitch", "pitch_name", "dur", "scale", "chord", "seq", "mini", "REST",
           "SCALES", "CHORDS"]
