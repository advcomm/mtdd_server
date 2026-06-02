#pragma once

#include <memory>

#include <grpcpp/security/server_credentials.h>

#include "config.h"

namespace mtdd::grpc_util {

std::shared_ptr<::grpc::ServerCredentials> BuildServerCredentials(const GrpcTlsConfig& tls);

}  // namespace mtdd::grpc_util
