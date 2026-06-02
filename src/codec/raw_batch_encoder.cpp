#include "codec/raw_batch_encoder.h"

#include <cstring>
#include <stdexcept>

namespace mtdd::codec {
namespace {

void AppendU32(std::string* out, uint32_t value) {
  const char bytes[4] = {
      static_cast<char>(value & 0xFF),
      static_cast<char>((value >> 8) & 0xFF),
      static_cast<char>((value >> 16) & 0xFF),
      static_cast<char>((value >> 24) & 0xFF),
  };
  out->append(bytes, sizeof(bytes));
}

void AppendBytes(std::string* out, const char* data, size_t length) {
  if (data != nullptr && length > 0) {
    out->append(data, length);
  }
}

}  // namespace

std::string EncodeRawPgBatch(PGresult* result, int row_begin, int row_end) {
  if (result == nullptr) {
    throw std::runtime_error("EncodeRawPgBatch result is null");
  }
  if (row_begin < 0 || row_end < row_begin) {
    throw std::runtime_error("EncodeRawPgBatch invalid row range");
  }

  const int num_cols = PQnfields(result);
  const int num_rows = row_end - row_begin;

  std::string out;
  out.reserve(static_cast<size_t>(16 + num_rows * num_cols * 8));

  AppendU32(&out, kRawPgBatchMagic);
  AppendU32(&out, kRawPgBatchVersion);
  AppendU32(&out, static_cast<uint32_t>(num_rows));
  AppendU32(&out, static_cast<uint32_t>(num_cols));

  for (int col = 0; col < num_cols; ++col) {
    for (int row = row_begin; row < row_end; ++row) {
      if (PQgetisnull(result, row, col)) {
        out.push_back('\x01');
        continue;
      }

      out.push_back('\x00');
      const char* value = PQgetvalue(result, row, col);
      const int length = PQgetlength(result, row, col);
      if (length < 0) {
        throw std::runtime_error("EncodeRawPgBatch negative cell length");
      }
      AppendU32(&out, static_cast<uint32_t>(length));
      AppendBytes(&out, value, static_cast<size_t>(length));
    }
  }

  return out;
}

}  // namespace mtdd::codec
