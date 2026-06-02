#include "health/pg_health_monitor.h"

#include <chrono>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "logging.h"

namespace mtdd::health {

PgHealthMonitor::PgHealthMonitor(int probe_interval_sec)
    : probe_interval_sec_(probe_interval_sec > 0 ? probe_interval_sec : 30) {}

PgHealthMonitor::~PgHealthMonitor() {
  Stop();
}

void PgHealthMonitor::AttachServer(grpc::Server* server) {
  server_ = server;
  SetServing(false);
}

void PgHealthMonitor::OnConnectSuccess(const pg::ConnectParams& params, int statement_timeout_ms) {
  {
    std::lock_guard<std::mutex> lock(params_mutex_);
    params_ = params;
    statement_timeout_ms_ = statement_timeout_ms;
    has_params_.store(true);
  }
  SetServing(ProbeOnce());

  if (!probe_thread_.joinable()) {
    stop_.store(false);
    probe_thread_ = std::thread([this]() { ProbeLoop(); });
  }
}

void PgHealthMonitor::OnConnectFailure() {
  SetServing(false);
}

void PgHealthMonitor::SetServingWithoutPg() {
  SetServing(true);
}

void PgHealthMonitor::Stop() {
  stop_.store(true);
  if (probe_thread_.joinable()) {
    probe_thread_.join();
  }
}

void PgHealthMonitor::SetServing(bool serving) {
  if (server_ == nullptr) {
    return;
  }
  auto* health = server_->GetHealthCheckService();
  if (health == nullptr) {
    return;
  }
  health->SetServingStatus("", serving);
  health->SetServingStatus("mtdd.MtddShard", serving);
  health->SetServingStatus("mtdd.MtddNotify", serving);
}

bool PgHealthMonitor::ProbeOnce() const {
  if (!has_params_.load()) {
    return false;
  }

  pg::ConnectParams params;
  int statement_timeout_ms = 0;
  {
    std::lock_guard<std::mutex> lock(params_mutex_);
    params = params_;
    statement_timeout_ms = statement_timeout_ms_;
  }

  std::string error;
  auto conn = pg::PqConnection::Connect(params, &error, statement_timeout_ms);
  return conn != nullptr;
}

void PgHealthMonitor::ProbeLoop() {
  while (!stop_.load()) {
    if (has_params_.load()) {
      const bool healthy = ProbeOnce();
      SetServing(healthy);
      if (!healthy) {
        log::Warn("health_probe_failed", "PostgreSQL probe failed");
      }
    }

    for (int i = 0; i < probe_interval_sec_ * 10 && !stop_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

}  // namespace mtdd::health
