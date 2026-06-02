#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <csignal>
#include <memory>
#include <optional>
#include <string>

#include "config.h"
#include "grpc/server_credentials.h"
#include "health/pg_health_monitor.h"
#include "logging.h"
#include "notify/registry.h"
#include "service/mtdd_notify_service.h"
#include "service/mtdd_shard_service.h"

namespace {

std::unique_ptr<grpc::Server> g_server;
mtdd::health::PgHealthMonitor* g_health_monitor = nullptr;

void HandleSignal(int /*signum*/) {
  if (g_health_monitor != nullptr) {
    g_health_monitor->Stop();
  }
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
    if (config.grpc_reflection) {
      grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    }

    mtdd::notify::NotifyRegistry notify_registry;
    mtdd::health::PgHealthMonitor health_monitor(config.health_probe_interval_sec);
    g_health_monitor = &health_monitor;

    mtdd::service::MtddShardServiceImpl shard_service(config, &health_monitor);
    std::optional<mtdd::service::MtddNotifyServiceImpl> notify_service;
    if (config.notify_enabled) {
      notify_service.emplace(notify_registry, config);
    }

    const std::string listen_target = config.listen_address + ":" + std::to_string(config.listen_port);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_target, mtdd::grpc_util::BuildServerCredentials(config.grpc_tls));
    builder.RegisterService(&shard_service);
    if (notify_service.has_value()) {
      builder.RegisterService(&notify_service.value());
    }
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS, config.grpc_max_threads);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, 1);
    builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, config.grpc_max_threads);

    g_server = builder.BuildAndStart();
    if (!g_server) {
      mtdd::log::Error("startup_failed", "BuildAndStart returned null");
      return 1;
    }

    health_monitor.AttachServer(g_server.get());
    if (!config.health_require_pg) {
      health_monitor.SetServingWithoutPg();
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    mtdd::log::Info("listening", listen_target + (config.grpc_tls.enabled ? " tls=1" : " tls=0"));
    g_server->Wait();

    health_monitor.Stop();
    mtdd::log::Info("shutdown", "");
    return 0;
  } catch (const std::exception& ex) {
    mtdd::log::Error("fatal", ex.what());
    return 1;
  }
}
