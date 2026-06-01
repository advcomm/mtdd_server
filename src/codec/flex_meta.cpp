#include "codec/flex_meta.h"

#include <flatbuffers/flexbuffers.h>

namespace mtdd::codec {
namespace {

std::string FinishMap(flexbuffers::Builder& builder) {
  const auto root = builder.GetBuffer();
  return std::string(reinterpret_cast<const char*>(root.data()), root.size());
}

void AddFieldInfo(flexbuffers::Builder& builder, const FieldInfo& field) {
  builder.Map([&]() {
    builder.String("name", field.name.c_str());
    builder.UInt("table_oid", field.table_oid);
    builder.UInt("column_id", field.column_id);
    builder.UInt("data_type_oid", field.data_type_oid);
    builder.Int("format", field.format);
  });
}

}  // namespace

std::string EncodeResultSchema(const ResultSchema& schema) {
  flexbuffers::Builder builder(256);
  builder.Map([&]() {
    builder.String("command", schema.command.c_str());
    builder.Vector("fields", [&]() {
      for (const auto& field : schema.fields) {
        AddFieldInfo(builder, field);
      }
    });
  });
  builder.Finish();
  return FinishMap(builder);
}

std::string EncodeResultTrailer(const ResultTrailer& trailer) {
  flexbuffers::Builder builder(128);
  builder.Map([&]() {
    builder.String("command_tag", trailer.command_tag.c_str());
    builder.Int("row_count", trailer.row_count);
    builder.UInt("oid", trailer.oid);
  });
  builder.Finish();
  return FinishMap(builder);
}

std::string EncodePgError(const PgError& error) {
  flexbuffers::Builder builder(256);
  builder.Map([&]() {
    builder.String("sqlstate", error.sqlstate.c_str());
    builder.String("severity", error.severity.c_str());
    builder.String("message", error.message.c_str());
    builder.String("detail", error.detail.c_str());
    builder.String("position", error.position.c_str());
  });
  builder.Finish();
  return FinishMap(builder);
}

}  // namespace mtdd::codec
