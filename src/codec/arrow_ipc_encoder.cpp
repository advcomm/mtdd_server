#include "codec/arrow_ipc_encoder.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace mtdd::codec {
namespace {

constexpr int kOidBool = 16;
constexpr int kOidBytea = 17;
constexpr int kOidInt8 = 20;
constexpr int kOidInt2 = 21;
constexpr int kOidInt4 = 23;
constexpr int kOidFloat4 = 700;
constexpr int kOidFloat8 = 701;
constexpr int kOidNumeric = 1700;
constexpr int kOidUuid = 2950;

std::string CommandFromStatus(const char* status) {
  if (status == nullptr || *status == '\0') {
    return "SELECT";
  }
  std::string tag(status);
  const auto space = tag.find_first_of(" \t");
  const std::string token = space == std::string::npos ? tag : tag.substr(0, space);
  if (token.empty()) {
    return "SELECT";
  }
  for (auto& ch : token) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return token;
}

arrow::Status AppendUtf8Column(
    arrow::StringBuilder* builder,
    PGresult* result,
    int col,
    int row_begin,
    int row_end) {
  for (int row = row_begin; row < row_end; ++row) {
    if (PQgetisnull(result, row, col)) {
      ARROW_RETURN_NOT_OK(builder->AppendNull());
      continue;
    }
    const char* value = PQgetvalue(result, row, col);
    ARROW_RETURN_NOT_OK(builder->Append(value != nullptr ? value : ""));
  }
  return arrow::Status::OK();
}

arrow::Status AppendBoolColumn(
    arrow::BooleanBuilder* builder,
    PGresult* result,
    int col,
    int row_begin,
    int row_end) {
  for (int row = row_begin; row < row_end; ++row) {
    if (PQgetisnull(result, row, col)) {
      ARROW_RETURN_NOT_OK(builder->AppendNull());
      continue;
    }
    const char* value = PQgetvalue(result, row, col);
    const bool b = value != nullptr && (value[0] == 't' || value[0] == 'T' || value[0] == '1');
    ARROW_RETURN_NOT_OK(builder->Append(b));
  }
  return arrow::Status::OK();
}

arrow::Status AppendInt32Column(
    arrow::Int32Builder* builder,
    PGresult* result,
    int col,
    int row_begin,
    int row_end) {
  for (int row = row_begin; row < row_end; ++row) {
    if (PQgetisnull(result, row, col)) {
      ARROW_RETURN_NOT_OK(builder->AppendNull());
      continue;
    }
    const char* value = PQgetvalue(result, row, col);
    ARROW_RETURN_NOT_OK(builder->Append(static_cast<int32_t>(std::strtol(value, nullptr, 10))));
  }
  return arrow::Status::OK();
}

arrow::Status AppendDoubleColumn(
    arrow::DoubleBuilder* builder,
    PGresult* result,
    int col,
    int row_begin,
    int row_end) {
  for (int row = row_begin; row < row_end; ++row) {
    if (PQgetisnull(result, row, col)) {
      ARROW_RETURN_NOT_OK(builder->AppendNull());
      continue;
    }
    const char* value = PQgetvalue(result, row, col);
    ARROW_RETURN_NOT_OK(builder->Append(std::strtod(value, nullptr)));
  }
  return arrow::Status::OK();
}

arrow::Status AppendBinaryColumn(
    arrow::BinaryBuilder* builder,
    PGresult* result,
    int col,
    int row_begin,
    int row_end) {
  for (int row = row_begin; row < row_end; ++row) {
    if (PQgetisnull(result, row, col)) {
      ARROW_RETURN_NOT_OK(builder->AppendNull());
      continue;
    }
    const char* value = PQgetvalue(result, row, col);
    const int length = PQgetlength(result, row, col);
    ARROW_RETURN_NOT_OK(builder->Append(reinterpret_cast<const uint8_t*>(value), length));
  }
  return arrow::Status::OK();
}

arrow::Status BuildColumnArray(
    PGresult* result,
    int col,
    int row_begin,
    int row_end,
    std::shared_ptr<arrow::Array>* out) {
  const int oid = PQftype(result, col);

  if (oid == kOidBool) {
    arrow::BooleanBuilder builder;
    ARROW_RETURN_NOT_OK(AppendBoolColumn(&builder, result, col, row_begin, row_end));
    return builder.Finish(out);
  }
  if (oid == kOidInt2 || oid == kOidInt4) {
    arrow::Int32Builder builder;
    ARROW_RETURN_NOT_OK(AppendInt32Column(&builder, result, col, row_begin, row_end));
    return builder.Finish(out);
  }
  if (oid == kOidFloat4 || oid == kOidFloat8) {
    arrow::DoubleBuilder builder;
    ARROW_RETURN_NOT_OK(AppendDoubleColumn(&builder, result, col, row_begin, row_end));
    return builder.Finish(out);
  }
  if (oid == kOidBytea) {
    arrow::BinaryBuilder builder;
    ARROW_RETURN_NOT_OK(AppendBinaryColumn(&builder, result, col, row_begin, row_end));
    return builder.Finish(out);
  }

  arrow::StringBuilder builder;
  ARROW_RETURN_NOT_OK(AppendUtf8Column(&builder, result, col, row_begin, row_end));
  return builder.Finish(out);
}

std::string SerializeBatch(const std::shared_ptr<arrow::RecordBatch>& batch) {
  arrow::io::BufferOutputStream out;
  auto writer_result = arrow::ipc::MakeStreamWriter(&out, batch->schema());
  if (!writer_result.ok()) {
    throw std::runtime_error(writer_result.status().ToString());
  }
  auto writer = *writer_result;
  const auto write_status = writer->WriteRecordBatch(*batch);
  if (!write_status.ok()) {
    throw std::runtime_error(write_status.ToString());
  }
  const auto close_status = writer->Close();
  if (!close_status.ok()) {
    throw std::runtime_error(close_status.ToString());
  }
  auto buffer_result = out.Finish();
  if (!buffer_result.ok()) {
    throw std::runtime_error(buffer_result.status().ToString());
  }
  auto buffer = *buffer_result;
  return buffer->ToString();
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

}  // namespace

ArrowStreamChunks EncodePgResult(PGresult* result, int batch_rows) {
  ArrowStreamChunks chunks;
  const ExecStatusType status = PQresultStatus(result);

  if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
    throw std::runtime_error("EncodePgResult called on non-result status");
  }

  chunks.schema = BuildSchemaMeta(result);
  const int num_rows = PQntuples(result);
  const int num_fields = PQnfields(result);

  const char* cmd_status = PQcmdStatus(result);
  std::ostringstream tag;
  tag << chunks.schema.command;
  if (status == PGRES_COMMAND_OK) {
    const char* tuples = PQcmdTuples(result);
    const int64_t affected = tuples != nullptr ? std::strtoll(tuples, nullptr, 10) : 0;
    chunks.trailer.command_tag = cmd_status != nullptr ? cmd_status : tag.str();
    chunks.trailer.row_count = affected;
    chunks.trailer.oid = 0;
    return chunks;
  }

  if (num_fields == 0) {
    tag << " " << num_rows;
    chunks.trailer.command_tag = cmd_status != nullptr ? cmd_status : tag.str();
    chunks.trailer.row_count = num_rows;
    chunks.trailer.oid = 0;
    return chunks;
  }

  const int effective_batch = batch_rows > 0 ? batch_rows : 10000;
  std::vector<std::shared_ptr<arrow::Field>> arrow_fields;
  arrow_fields.reserve(num_fields);
  for (int col = 0; col < num_fields; ++col) {
    const int oid = PQftype(result, col);
    std::shared_ptr<arrow::DataType> type = arrow::utf8();
    if (oid == kOidBool) {
      type = arrow::boolean();
    } else if (oid == kOidInt2 || oid == kOidInt4) {
      type = arrow::int32();
    } else if (oid == kOidFloat4 || oid == kOidFloat8) {
      type = arrow::double();
    } else if (oid == kOidBytea) {
      type = arrow::binary();
    }
    arrow_fields.push_back(arrow::field(PQfname(result, col), type));
  }
  auto schema = arrow::schema(arrow_fields);

  for (int row_begin = 0; row_begin < num_rows; row_begin += effective_batch) {
    const int row_end = std::min(row_begin + effective_batch, num_rows);
    std::vector<std::shared_ptr<arrow::Array>> arrays;
    arrays.reserve(num_fields);
    for (int col = 0; col < num_fields; ++col) {
      std::shared_ptr<arrow::Array> array;
      auto st = BuildColumnArray(result, col, row_begin, row_end, &array);
      if (!st.ok()) {
        throw std::runtime_error(st.ToString());
      }
      arrays.push_back(std::move(array));
    }
    auto batch = arrow::RecordBatch::Make(schema, row_end - row_begin, arrays);
    chunks.ipc_batches.push_back(SerializeBatch(batch));
  }

  tag << " " << num_rows;
  chunks.trailer.command_tag = cmd_status != nullptr ? cmd_status : tag.str();
  chunks.trailer.row_count = num_rows;
  chunks.trailer.oid = 0;
  return chunks;
}

}  // namespace mtdd::codec
