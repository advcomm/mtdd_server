#pragma once

#include <grpcpp/grpcpp.h>
#include <mtdd.grpc.pb.h>

#include "config.h"
#include "notify/registry.h"

namespace mtdd::service {

class MtddNotifyServiceImpl final : public mtdd::MtddNotify::Service {
 public:
  MtddNotifyServiceImpl(notify::NotifyRegistry& registry, const ServerConfig& config);

  grpc::Status Subscribe(grpc::ServerContext* context, const mtdd::NotifySubscribeRequest* request,
                         mtdd::NotifyAck* response) override;

  grpc::Status Unsubscribe(grpc::ServerContext* context, const mtdd::NotifyUnsubscribeRequest* request,
                           mtdd::NotifyAck* response) override;

  grpc::Status UnsubscribeAll(grpc::ServerContext* context,
                              const mtdd::NotifyUnsubscribeAllRequest* request,
                              mtdd::NotifyAck* response) override;

  grpc::Status Publish(grpc::ServerContext* context, const mtdd::NotifyPublishRequest* request,
                       mtdd::NotifyAck* response) override;

  grpc::Status Watch(grpc::ServerContext* context, const mtdd::NotifyWatchRequest* request,
                     grpc::ServerWriter<mtdd::NotifyMessage>* writer) override;

 private:
  grpc::Status ValidateNotifyLimits(const std::string& channel, const std::string& payload) const;

  notify::NotifyRegistry& registry_;
  ServerConfig config_;
};

}  // namespace mtdd::service
