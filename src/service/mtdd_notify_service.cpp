#include "service/mtdd_notify_service.h"

#include <chrono>
#include <thread>

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

MtddNotifyServiceImpl::MtddNotifyServiceImpl(notify::NotifyRegistry& registry) : registry_(registry) {}

grpc::Status MtddNotifyServiceImpl::Subscribe(grpc::ServerContext* /*context*/,
                                              const mtdd::NotifySubscribeRequest* request,
                                              mtdd::NotifyAck* response) {
  if (const grpc::Status status = ValidateClientId(request->client_id()); !status.ok()) {
    return status;
  }
  if (const grpc::Status status = ValidateChannel(request->channel()); !status.ok()) {
    return status;
  }

  registry_.Subscribe(request->client_id(), request->channel(), request->tid_scope());
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
  SetAck(response, true, "unsubscribed all");
  return grpc::Status::OK;
}

grpc::Status MtddNotifyServiceImpl::Publish(grpc::ServerContext* /*context*/,
                                            const mtdd::NotifyPublishRequest* request,
                                            mtdd::NotifyAck* response) {
  if (const grpc::Status status = ValidateChannel(request->channel()); !status.ok()) {
    return status;
  }

  const int delivered = registry_.Publish(request->channel(), request->payload(), request->tid_scope());
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
  while (!context->IsCancelled()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  registry_.UnregisterWatch(request->client_id(), writer);
  return grpc::Status::OK;
}

}  // namespace mtdd::service
