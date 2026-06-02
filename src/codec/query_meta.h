#pragma once

#include <libpq-fe.h>

#include <string>

#include "codec/flex_meta.h"

namespace mtdd::codec {

ResultSchema BuildSchemaMeta(PGresult* result);

ResultTrailer BuildCommandTrailer(PGresult* result);

bool IsTupleStreamingSql(const std::string& sql);

}  // namespace mtdd::codec
