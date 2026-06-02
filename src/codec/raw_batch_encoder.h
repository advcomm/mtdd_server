#pragma once

#include <libpq-fe.h>

#include <cstdint>
#include <string>

namespace mtdd::codec {

constexpr uint32_t kRawPgBatchMagic = 0x42504752;  // 'RPGB'
constexpr uint32_t kRawPgBatchVersion = 1;

// RPGB v1 batch encoder (matches @advcomm/mtdd pg-binary-decode.ts).
//
// RPGB framing (always little-endian uint32):
//   magic, version, num_rows, num_cols
// Per cell: uint8 is_null (1 = NULL); if non-null: uint32 len + raw bytes
//
// Cell bytes are opaque libpq PQgetvalue() payloads (no server-side decode or
// byte swap). PostgreSQL binary format (client decodes by OID + field.format):
//   int2/int4/int8, date, timestamp/timestamptz, numeric: big-endian
//   float4/float8: IEEE 754 in PostgreSQL server native byte order
//   bool, bytea, uuid: opaque bytes
// Production assumes PostgreSQL on little-endian hosts (Linux x86_64/aarch64).
//
// Column-major libpq cell passthrough for rows [row_begin, row_end).
std::string EncodeRawPgBatch(PGresult* result, int row_begin, int row_end);

}  // namespace mtdd::codec
