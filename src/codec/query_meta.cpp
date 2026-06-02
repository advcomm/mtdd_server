#include "codec/query_meta.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

namespace mtdd::codec {
namespace {

std::string CommandFromStatus(const char* status) {
  if (status == nullptr || *status == '\0') {
    return "SELECT";
  }
  std::string tag(status);
  const auto space = tag.find_first_of(" \t");
  std::string token = space == std::string::npos ? tag : tag.substr(0, space);
  if (token.empty()) {
    return "SELECT";
  }
  for (size_t i = 0; i < token.size(); ++i) {
    token[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(token[i])));
  }
  return token;
}

std::string TrimSql(const std::string& sql) {
  const auto begin = sql.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = sql.find_last_not_of(" \t\r\n");
  return sql.substr(begin, end - begin + 1);
}

std::string UpperToken(const std::string& sql) {
  const auto trimmed = TrimSql(sql);
  const auto space = trimmed.find_first_of(" \t\r\n(");
  std::string token = space == std::string::npos ? trimmed : trimmed.substr(0, space);
  for (char& ch : token) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return token;
}

}  // namespace

bool IsTupleStreamingSql(const std::string& sql) {
  const std::string token = UpperToken(sql);
  return token == "SELECT" || token == "WITH" || token == "TABLE" || token == "VALUES";
}

ResultSchema BuildSchemaMeta(PGresult* result) {
  ResultSchema schema;
  schema.command = CommandFromStatus(PQcmdStatus(result));

  const int fields = PQnfields(result);
  schema.fields.reserve(fields);
  for (int col = 0; col < fields; ++col) {
    FieldInfo info;
    info.name = PQfname(result, col) != nullptr ? PQfname(result, col) : "";
    info.table_oid = static_cast<uint32_t>(PQftable(result, col));
    info.column_id = static_cast<uint32_t>(PQftablecol(result, col));
    info.data_type_oid = static_cast<uint32_t>(PQftype(result, col));
    info.format = PQfformat(result, col);
    schema.fields.push_back(std::move(info));
  }
  return schema;
}

ResultTrailer BuildCommandTrailer(PGresult* result) {
  ResultTrailer trailer;
  const ExecStatusType status = PQresultStatus(result);
  const char* cmd_status = PQcmdStatus(result);
  const std::string command = CommandFromStatus(cmd_status);

  std::ostringstream tag;
  tag << command;

  if (status == PGRES_COMMAND_OK) {
    const char* tuples = PQcmdTuples(result);
    trailer.row_count = tuples != nullptr ? std::strtoll(tuples, nullptr, 10) : 0;
    trailer.command_tag = cmd_status != nullptr ? cmd_status : tag.str();
    trailer.oid = 0;
    return trailer;
  }

  const int num_rows = PQntuples(result);
  tag << " " << num_rows;
  trailer.row_count = num_rows;
  trailer.command_tag = cmd_status != nullptr ? cmd_status : tag.str();
  trailer.oid = 0;
  return trailer;
}

}  // namespace mtdd::codec
