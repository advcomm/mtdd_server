#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mtdd::codec {

struct FieldInfo {
  std::string name;
  uint32_t table_oid = 0;
  uint32_t column_id = 0;
  uint32_t data_type_oid = 0;
  int format = 0;
};

struct ResultSchema {
  std::string command;
  std::vector<FieldInfo> fields;
};

struct ResultTrailer {
  std::string command_tag;
  int64_t row_count = 0;
  uint32_t oid = 0;
};

struct PgError {
  std::string sqlstate;
  std::string severity = "ERROR";
  std::string message;
  std::string detail;
  std::string position;
};

std::string EncodeResultSchema(const ResultSchema& schema);
std::string EncodeResultTrailer(const ResultTrailer& trailer);
std::string EncodePgError(const PgError& error);

}  // namespace mtdd::codec
