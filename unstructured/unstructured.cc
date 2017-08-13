#include <grpc++/unstructured.h>

#include <atomic>
#include <thread>

#include <grpc/support/log.h>
#include <grpc++/generic/async_generic_service.h>
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
  AsyncGenericService* generic_service;
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

Server::Builder& Server::Builder::RegisterAsyncGenericService(AsyncGenericService* service) {
  builder_.RegisterAsyncGenericService(service);
  generic_service_ = service;
  return *this;
}

Server Server::Builder::BuildAndStart() {
  return Server({builder_.AddCompletionQueue(),
                 builder_.BuildAndStart(),
                 std::move(handlers_),
                 generic_service_});
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
  class CallData;
  void HandleRpcs();

  ServerWithFriends server_with_friends_;
  std::thread handle_rpcs_thread_;
};

class Server::Impl::CallData final : public CallDataBase {
 public:
  CallData(AsyncGenericService* service, ServerCompletionQueue* cq)
      : generic_service_(service), cq_(cq) {
    generic_service_->RequestCall(&ctx_, &stream_, cq_, cq_, this);
  }

  void Proceed(bool ok) override {
    if (!ok) status_ = CallStatus::FINISH;

    switch (status_) {
      case CallStatus::PROCESS: {
        new CallData(generic_service_, cq_);

        ctx_.SetHTML();

        static std::atomic<int> count(0);
        static constexpr int kSize = 1024;
        std::unique_ptr<char[]> chars(new char[kSize]);
        const size_t len = std::max(0, std::min(kSize - 1,
            snprintf(chars.get(), kSize,
"<html><head><link rel=icon href=\"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAA"
"AABJRU5ErkJggg==\"></head>"
"<body>This <b>is</b> Навуходоносор. 小米科技. Method: %s. Count: %d.</body></html>",
                     ctx_.method().c_str(), count++)));
        Slice s(SliceFromCharArray(std::move(chars), len), Slice::STEAL_REF);

        stream_.WriteAndFinish(ByteBuffer(&s, 1), WriteOptions().set_raw(),
                               Status::OK, this);
        status_ = CallStatus::FINISH;
        break;
      }
      case CallStatus::FINISH: {
        delete this;
        break;
      }
    }
  }

 private:
  AsyncGenericService* const generic_service_;
  ServerCompletionQueue* const cq_;
  GenericServerContext ctx_;
  GenericServerAsyncReaderWriter stream_{&ctx_};
  enum class CallStatus { PROCESS, FINISH };
  CallStatus status_{CallStatus::PROCESS};
};

// This can be run in multiple threads if needed.
void Server::Impl::HandleRpcs() {
  gpr_log(GPR_ERROR, "Handling RPCs");
  // Spawn a new CallData instance to serve new clients.
  auto* cq = server_with_friends_.cq.get();
  for (const auto& handler : server_with_friends_.handlers) {
    handler.ptr->NewCallData(cq, handler.idx);
  }
  new CallData(server_with_friends_.generic_service, cq);
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

Server::Server() = default;
Server::Server(ServerWithFriends swf) : impl_(new Impl(std::move(swf))) {}
Server::Server(Server&& other) = default;
Server::~Server() = default;

Server::Builder::Builder() = default;
Server::Builder::~Builder() = default;

}  // namespace unstructured
}  // namespace grpc
