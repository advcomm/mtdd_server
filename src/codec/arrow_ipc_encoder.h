#pragma once

#include <libpq-fe.h>

#include <string>
#include <vector>

#include "codec/flex_meta.h"

namespace mtdd::codec {

struct ArrowStreamChunks {
  ResultSchema schema;
  std::vector<std::string> ipc_batches;
  ResultTrailer trailer;
};

ArrowStreamChunks EncodePgResult(PGresult* result, int batch_rows);

}  // namespace mtdd::codec
