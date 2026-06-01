#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <csignal>
#include <memory>
#include <string>

#include "config.h"
#include "logging.h"
#include "service/mtdd_shard_service.h"

namespace {

std::unique_ptr<grpc::Server> g_server;

void HandleSignal(int /*signum*/) {
  if (g_server) {
    g_server->Shutdown();
  }
}

}  // namespace

int main() {
  try {
    const mtdd::ServerConfig config = mtdd::LoadConfigFromEnv();
    mtdd::log::Info("starting", config.listen_address + ":" + std::to_string(config.listen_port));

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    mtdd::service::MtddShardServiceImpl service(config);

    const std::string listen_target = config.listen_address + ":" + std::to_string(config.listen_port);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_target, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS, config.grpc_max_threads);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, 1);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, config.grpc_max_threads);

    g_server = builder.BuildAndStart();
    if (!g_server) {
      mtdd::log::Error("startup_failed", "BuildAndStart returned null");
      return 1;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    mtdd::log::Info("listening", listen_target);
    g_server->Wait();
    mtdd::log::Info("shutdown", "");
    return 0;
  } catch (const std::exception& ex) {
    mtdd::log::Error("fatal", ex.what());
    return 1;
  }
}
