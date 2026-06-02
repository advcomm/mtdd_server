#pragma once

#include <grpcpp/grpcpp.h>
#include <mtdd.grpc.pb.h>

#include "notify/registry.h"

namespace mtdd::service {

class MtddNotifyServiceImpl final : public mtdd::MtddNotify::Service {
 public:
  explicit MtddNotifyServiceImpl(notify::NotifyRegistry& registry);

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
  notify::NotifyRegistry& registry_;
};

}  // namespace mtdd::service
