#include <grpc++/unstructured.h>

#include <thread>

#include <grpc/support/log.h>
#include <grpc++/impl/codegen/method_handler_impl.h>

namespace grpc {
namespace unstructured {

struct Server::Handler {
  std::unique_ptr<MethodHandler> ptr;
  size_t idx;
};

struct Server::ServerWithFriends {
  std::unique_ptr<ServerCompletionQueue> cq;
  std::unique_ptr<grpc::Server> server;
  std::vector<Handler> handlers;
};

Server::Builder& Server::Builder::AddListeningPort(
    const std::string& addr,
    std::shared_ptr<ServerCredentials> creds,
    int* selected_port) {
  builder_.AddListeningPort(addr, std::move(creds), selected_port);
  return *this;
}

Server::Builder& Server::Builder::RegisterService(Service* service) {
    builder_.RegisterService(service);
    const auto& methods = service->methods();
    for (size_t idx = 0; idx < methods.size(); ++idx) {
      handlers_.push_back({methods[idx]->ReleaseHandler(), idx});
    }
    return *this;
  }

Server Server::Builder::BuildAndStart() {
  return Server({builder_.AddCompletionQueue(),
                 builder_.BuildAndStart(),
                 std::move(handlers_)});
}

class Server::Impl final {
 public:
  explicit Impl(ServerWithFriends swf) {
    server_with_friends_ = std::move(swf);
    handle_rpcs_thread_ = std::thread([this] { HandleRpcs(); });
  }

  ~Impl() {
    server_with_friends_.server->Shutdown();
    server_with_friends_.cq->Shutdown();
    gpr_log(GPR_ERROR, "Server shutting down");
    handle_rpcs_thread_.join();
  }

 private:
  // This can be run in multiple threads if needed.
  void HandleRpcs() {
    gpr_log(GPR_ERROR, "Handling RPCs");
    // Spawn a new CallData instance to serve new clients.
    auto* cq = server_with_friends_.cq.get();
    for (const auto& handler : server_with_friends_.handlers) {
      handler.ptr->NewCallData(cq, handler.idx);
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

  ServerWithFriends server_with_friends_;
  std::thread handle_rpcs_thread_;
};

Server::Server() = default;
Server::Server(ServerWithFriends swf) : impl_(new Impl(std::move(swf))) {}
Server::Server(Server&& other) = default;
Server::~Server() = default;

Server::Builder::Builder() = default;
Server::Builder::~Builder() = default;

}  // namespace unstructured
}  // namespace grpc
