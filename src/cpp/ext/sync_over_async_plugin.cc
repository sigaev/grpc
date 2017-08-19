#include <grpc++/ext/sync_over_async_plugin.h>
#include <grpc++/impl/server_initializer.h>
#include <grpc++/server_builder.h>

namespace grpc {

namespace {

thread_local std::function<void(AsyncGenericService*, ServerCompletionQueue*)>
    t_generic_call_data_newer;

}  // namespace

void SyncOverAsyncPlugin::UpdateServerBuilder(ServerBuilder* builder) {
  builder->sync_over_async_.enabled = true;
  for (const auto& service : builder->services_) {
    builder->sync_over_async_.HarvestCallDataNewers(service->service);
  }
  builder->sync_over_async_.generic_call_data_newer =
      std::move(t_generic_call_data_newer);
}

// static
void SyncOverAsyncPlugin::SetGenericCallDataNewer(
    const std::function<void(AsyncGenericService*, ServerCompletionQueue*)>&
        generic_call_data_newer) {
  t_generic_call_data_newer = generic_call_data_newer;
}

}  // namespace grpc
