#include <grpc++/ext/sync_over_async_plugin.h>
#include <grpc++/impl/server_initializer.h>
#include <grpc++/server_builder.h>
#include <grpc/support/log.h>

namespace grpc {

SyncOverAsyncPlugin::SyncOverAsyncPlugin(
    std::function<void(AsyncGenericService*, ServerCompletionQueue*)>
        generic_call_data_factory)
    : generic_call_data_factory_(std::move(generic_call_data_factory)) {}

void SyncOverAsyncPlugin::UpdateServerBuilder(ServerBuilder* builder) {
  GPR_ASSERT(builder->sync_over_async_ == nullptr);
  builder->sync_over_async_.reset(new ServerBuilder::SyncOverAsync(
      builder->services_, generic_call_data_factory_));
}

}  // namespace grpc
