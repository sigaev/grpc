#include <grpc++/unstructured.h>

#include <thread>

#include <grpc/support/log.h>

namespace grpc {
namespace unstructured {

class Server::Impl final {
 public:
  explicit Impl(ServerWithServices sws) {
    server_with_services_ = std::move(sws);
    handle_rpcs_thread_ = std::thread([this] { HandleRpcs(); });
  }

  ~Impl() {
    server_with_services_.server->Shutdown();
    server_with_services_.cq->Shutdown();
    gpr_log(GPR_ERROR, "Server shutting down");
    handle_rpcs_thread_.join();
  }

 private:
  // This can be run in multiple threads if needed.
  void HandleRpcs() {
    // Spawn a new CallData instance to serve new clients.
    auto* cq = server_with_services_.cq.get();
    for (const auto& service : server_with_services_.services) {
      service->NewCallData(cq);
    }
    void* tag;  // uniquely identifies a request.
    bool ok;
    // Block waiting to read the next event from the completion queue. The
    // event is uniquely identified by its tag, which in this case is the
    // memory address of a CallData instance.
    // The return value of Next should always be checked. This return value
    // tells us whether there is any kind of event or cq is shutting down.
    while (cq->Next(&tag, &ok)) {
      static_cast<CallDataBase*>(tag)->Proceed(ok);
    }
  }

  ServerWithServices server_with_services_;
  std::thread handle_rpcs_thread_;
};

Server::Server(ServerWithServices sws) : impl_(new Impl(std::move(sws))) {}
Server::Server(Server&& other) = default;
Server::~Server() = default;

}  // namespace unstructured
}  // namespace grpc
