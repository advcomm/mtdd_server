#include <flatbuffers/flexbuffers.h>
#include <gtest/gtest.h>

#include "codec/flex_meta.h"

namespace {

flexbuffers::Reference ToRef(const std::string& bytes) {
  return flexbuffers::GetRoot(
      reinterpret_cast<const uint8_t*>(bytes.data()),
      bytes.size());
}

}  // namespace

TEST(FlexMetaTest, EncodesResultSchema) {
  mtdd::codec::ResultSchema schema;
  schema.command = "SELECT";
  schema.fields.push_back(
      {"id", 1, 1, 23, 0});

  const auto encoded = mtdd::codec::EncodeResultSchema(schema);
  const auto root = ToRef(encoded).AsMap();
  EXPECT_EQ(root["command"].AsString(), "SELECT");
  const auto fields = root["fields"].AsVector();
  ASSERT_EQ(fields.size(), 1u);
  const auto field = fields[0].AsMap();
  EXPECT_EQ(field["name"].AsString(), "id");
  EXPECT_EQ(field["data_type_oid"].AsUInt32(), 23u);
}

TEST(FlexMetaTest, EncodesTrailerAndError) {
  mtdd::codec::ResultTrailer trailer;
  trailer.command_tag = "SELECT 2";
  trailer.row_count = 2;
  trailer.oid = 0;

  const auto trailer_bytes = mtdd::codec::EncodeResultTrailer(trailer);
  const auto trailer_map = ToRef(trailer_bytes).AsMap();
  EXPECT_EQ(trailer_map["command_tag"].AsString(), "SELECT 2");
  EXPECT_EQ(trailer_map["row_count"].AsInt64(), 2);

  mtdd::codec::PgError error;
  error.sqlstate = "23505";
  error.message = "duplicate key";
  const auto error_bytes = mtdd::codec::EncodePgError(error);
  const auto error_map = ToRef(error_bytes).AsMap();
  EXPECT_EQ(error_map["sqlstate"].AsString(), "23505");
  EXPECT_EQ(error_map["message"].AsString(), "duplicate key");
}
