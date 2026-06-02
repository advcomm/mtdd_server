#pragma once

#include <grpcpp/grpcpp.h>
#include <mtdd.grpc.pb.h>

#include <memory>
#include <mutex>
#include <optional>

#include "config.h"
#include "pg/connection_manager.h"
#include "pg/query_executor.h"
#include "pg/session_store.h"

namespace mtdd::service {

class MtddShardServiceImpl final : public mtdd::MtddShard::Service {
 public:
  explicit MtddShardServiceImpl(ServerConfig config);

  grpc::Status Connect(grpc::ServerContext* context, const mtdd::ConnectRequest* request,
                       mtdd::ConnectResponse* response) override;

  grpc::Status QueryStream(grpc::ServerContext* context, const mtdd::QueryRequest* request,
                           grpc::ServerWriter<mtdd::ResultChunk>* writer) override;

  grpc::Status Disconnect(grpc::ServerContext* context, const mtdd::DisconnectRequest* request,
                          mtdd::DisconnectResponse* response) override;

 private:
  bool ValidateHostIndex(int32_t host_index, std::string* message) const;
  pg::ConnectParams BuildConnectParams(const mtdd::ConnectRequest& request) const;
  bool EnsureArrowFormat(const mtdd::QueryRequest& request, std::string* message) const;
  void WriteErrorChunk(grpc::ServerWriter<mtdd::ResultChunk>* writer, const pg::PgErrorMeta& error);

  ServerConfig config_;
  mutable std::mutex state_mutex_;
  pg::ConnectionManager pool_;
  pg::SessionStore sessions_;
  std::optional<pg::ConnectParams> connect_params_;
  std::unique_ptr<pg::QueryExecutor> executor_;
};

}  // namespace mtdd::service
