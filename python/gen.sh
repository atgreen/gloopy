#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# Regenerate the protobuf/gRPC Python stubs from proto/gloopy.proto.
# Run after editing the proto. Requires `pip install grpcio-tools`.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

python3 -m grpc_tools.protoc \
    --proto_path="$ROOT/proto" \
    --python_out="$HERE/gloopy" \
    --grpc_python_out="$HERE/gloopy" \
    "$ROOT/proto/gloopy.proto"

# grpc_tools emits a top-level `import gloopy_pb2`; make it package-relative so
# the stub works as `gloopy.gloopy_pb2_grpc`.
sed -i 's/^import gloopy_pb2 as/from . import gloopy_pb2 as/' "$HERE/gloopy/gloopy_pb2_grpc.py"
echo "Regenerated python/gloopy/gloopy_pb2{,_grpc}.py"
