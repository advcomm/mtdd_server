#!/usr/bin/env bash
# Compare local proto/mtdd.proto with upstream advcomm/mtdd.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Default: @advcomm/mtdd RPGB client (grpc-query-codec, ResultChunk.payload, result_format=1).
REF="${MTDD_PROTO_REF:-07c20bcad5a6cbdfe76f885d00cee16723ba7849}"
UPSTREAM="${MTDD_PROTO_URL:-https://raw.githubusercontent.com/advcomm/mtdd/${REF}/proto/mtdd.proto}"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
curl -fsSL "$UPSTREAM" -o "$TMP"
if diff -q "$ROOT/proto/mtdd.proto" "$TMP" >/dev/null; then
  echo "proto/mtdd.proto matches upstream (${REF})"
else
  echo "proto/mtdd.proto differs from upstream (${UPSTREAM})" >&2
  diff -u "$ROOT/proto/mtdd.proto" "$TMP" || true
  exit 1
fi
