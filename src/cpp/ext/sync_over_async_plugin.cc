#include <grpc++/ext/sync_over_async_plugin.h>
#include <grpc++/impl/server_initializer.h>
#include <grpc++/server_builder.h>
#include <grpc/support/log.h>

namespace grpc {

namespace {

thread_local std::function<void(AsyncGenericService*, ServerCompletionQueue*)>
    t_generic_call_data_factory;

}  // namespace

void SyncOverAsyncPlugin::UpdateServerBuilder(ServerBuilder* builder) {
  GPR_ASSERT(builder->sync_over_async_ == nullptr);
  builder->sync_over_async_.reset(new ServerBuilder::SyncOverAsync(
      builder->services_, std::move(t_generic_call_data_factory)));
}

// static
void SyncOverAsyncPlugin::SetGenericCallDataFactory(
    const std::function<void(AsyncGenericService*, ServerCompletionQueue*)>&
        generic_call_data_factory) {
  t_generic_call_data_factory = generic_call_data_factory;
}

}  // namespace grpc
