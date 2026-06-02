#pragma once

#include <libpq-fe.h>

#include <cstdint>
#include <string>

namespace mtdd::codec {

constexpr uint32_t kRawPgBatchMagic = 0x42504752;  // 'RPGB'
constexpr uint32_t kRawPgBatchVersion = 1;

// Column-major libpq cell passthrough for rows [row_begin, row_end).
std::string EncodeRawPgBatch(PGresult* result, int row_begin, int row_end);

}  // namespace mtdd::codec
