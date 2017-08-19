#ifndef GRPCXX_EXT_SYNC_OVER_ASYNC_PLUGIN_H
#define GRPCXX_EXT_SYNC_OVER_ASYNC_PLUGIN_H

#include <functional>

#include <grpc++/impl/server_builder_plugin.h>

namespace grpc {

class AsyncGenericService;
class ServerCompletionQueue;

class SyncOverAsyncPlugin final : public ServerBuilderPlugin {
 public:
  explicit SyncOverAsyncPlugin(
      std::function<void(AsyncGenericService*, ServerCompletionQueue*)>
          generic_call_data_factory = nullptr);

 private:
  void UpdateServerBuilder(ServerBuilder* builder) override;
  void InitServer(ServerInitializer* si) override {}
  void Finish(ServerInitializer* si) override {}
  void ChangeArguments(const grpc::string& name, void* value) override {}
  void UpdateChannelArguments(ChannelArguments* args) override {}
  grpc::string name() override { return "sync_over_async"; }

  const std::function<void(AsyncGenericService*, ServerCompletionQueue*)>
      generic_call_data_factory_;
};

}  // namespace grpc

#endif  // GRPCXX_EXT_SYNC_OVER_ASYNC_PLUGIN_H
