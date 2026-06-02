#include "service/mtdd_notify_service.h"

#include <chrono>
#include <thread>

#include "logging.h"

namespace mtdd::service {
namespace {

grpc::Status ValidateClientId(const std::string& client_id) {
  if (client_id.empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "client_id is required");
  }
  return grpc::Status::OK;
}

grpc::Status ValidateChannel(const std::string& channel) {
  if (channel.empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "channel is required");
  }
  return grpc::Status::OK;
}

void SetAck(mtdd::NotifyAck* response, bool ok, const std::string& message, int delivered_count = 0) {
  response->set_ok(ok);
  response->set_message(message);
  response->set_delivered_count(delivered_count);
}

}  // namespace

MtddNotifyServiceImpl::MtddNotifyServiceImpl(notify::NotifyRegistry& registry, const ServerConfig& config)
    : registry_(registry), config_(config) {}

grpc::Status MtddNotifyServiceImpl::ValidateNotifyLimits(const std::string& channel,
                                                         const std::string& payload) const {
  if (static_cast<int>(channel.size()) > config_.max_notify_channel_bytes) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "channel exceeds MTDD_MAX_NOTIFY_CHANNEL_BYTES");
  }
  if (static_cast<int>(payload.size()) > config_.max_notify_payload_bytes) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "payload exceeds MTDD_MAX_NOTIFY_PAYLOAD_BYTES");
  }
  return grpc::Status::OK;
}

grpc::Status MtddNotifyServiceImpl::Subscribe(grpc::ServerContext* /*context*/,
                                              const mtdd::NotifySubscribeRequest* request,
                                              mtdd::NotifyAck* response) {
  if (const grpc::Status status = ValidateClientId(request->client_id()); !status.ok()) {
    return status;
  }
  if (const grpc::Status status = ValidateChannel(request->channel()); !status.ok()) {
    return status;
  }
  if (const grpc::Status status = ValidateNotifyLimits(request->channel(), ""); !status.ok()) {
    return status;
  }

  registry_.Subscribe(request->client_id(), request->channel(), request->tid_scope());
  log::Info("notify_subscribe", request->client_id() + " " + request->channel());
  SetAck(response, true, "subscribed");
  return grpc::Status::OK;
}

grpc::Status MtddNotifyServiceImpl::Unsubscribe(grpc::ServerContext* /*context*/,
                                                const mtdd::NotifyUnsubscribeRequest* request,
                                                mtdd::NotifyAck* response) {
  if (const grpc::Status status = ValidateClientId(request->client_id()); !status.ok()) {
    return status;
  }
  if (const grpc::Status status = ValidateChannel(request->channel()); !status.ok()) {
    return status;
  }

  registry_.Unsubscribe(request->client_id(), request->channel(), request->tid_scope());
  log::Info("notify_unsubscribe", request->client_id() + " " + request->channel());
  SetAck(response, true, "unsubscribed");
  return grpc::Status::OK;
}

grpc::Status MtddNotifyServiceImpl::UnsubscribeAll(grpc::ServerContext* /*context*/,
                                                     const mtdd::NotifyUnsubscribeAllRequest* request,
                                                     mtdd::NotifyAck* response) {
  if (const grpc::Status status = ValidateClientId(request->client_id()); !status.ok()) {
    return status;
  }

  registry_.UnsubscribeAll(request->client_id());
  log::Info("notify_unsubscribe_all", request->client_id());
  SetAck(response, true, "unsubscribed all");
  return grpc::Status::OK;
}

grpc::Status MtddNotifyServiceImpl::Publish(grpc::ServerContext* /*context*/,
                                            const mtdd::NotifyPublishRequest* request,
                                            mtdd::NotifyAck* response) {
  if (const grpc::Status status = ValidateChannel(request->channel()); !status.ok()) {
    return status;
  }
  if (const grpc::Status status = ValidateNotifyLimits(request->channel(), request->payload()); !status.ok()) {
    return status;
  }

  const int delivered = registry_.Publish(request->channel(), request->payload(), request->tid_scope());
  log::Info("notify_publish", request->channel() + " delivered=" + std::to_string(delivered));
  SetAck(response, true, "published", delivered);
  return grpc::Status::OK;
}

grpc::Status MtddNotifyServiceImpl::Watch(grpc::ServerContext* context,
                                          const mtdd::NotifyWatchRequest* request,
                                          grpc::ServerWriter<mtdd::NotifyMessage>* writer) {
  if (const grpc::Status status = ValidateClientId(request->client_id()); !status.ok()) {
    return status;
  }

  registry_.RegisterWatch(request->client_id(), writer);
  log::Info("notify_watch", request->client_id());
  while (!context->IsCancelled()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  registry_.UnregisterWatch(request->client_id(), writer);
  log::Info("notify_watch_end", request->client_id());
  return grpc::Status::OK;
}

}  // namespace mtdd::service
