#ifndef GRPCXX_UNSTRUCTURED_H
#define GRPCXX_UNSTRUCTURED_H

#include <deque>
#include <functional>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

namespace grpc {
namespace unstructured {

class Server final {
 public:
  class Builder;

  Server(Server&& other);
  ~Server();

 private:
  class Impl;
  struct ServerWithServices;

  class CallDataBase {
   public:
    CallDataBase() = default;

    virtual ~CallDataBase() {}
    virtual void Proceed(bool ok) = 0;

   private:
    CallDataBase(const CallDataBase& other) = delete;
    CallDataBase& operator=(const CallDataBase& other) = delete;
  };

  class ServiceBase {
   public:
    ServiceBase() = default;
    virtual ~ServiceBase() {}
    virtual void NewCallData(ServerCompletionQueue* cq) = 0;

   private:
    ServiceBase(const ServiceBase& other) = delete;
    ServiceBase& operator=(const ServiceBase& other) = delete;
  };

  explicit Server(ServerWithServices sws);

  std::unique_ptr<Impl> impl_;

  Server(const Server& other) = delete;
  Server& operator=(const Server& other) = delete;
};

class Server::Builder final {
 public:
  Builder() = default;

  Builder& AddListeningPort(const std::string& addr,
                            std::shared_ptr<ServerCredentials> creds,
                            int* selected_port = nullptr);

  template <class S, class X, class Y>
  Builder& AddService(std::function<void(const X&, Y*)> handler) {
    auto* service = new Service<S, X, Y>(std::move(handler));
    services_.emplace_back(service);
    builder_.RegisterService(service->get());
    return *this;
  }

  Server BuildAndStart();

 private:
  template <class, class, class> class Service;

  grpc::ServerBuilder builder_;
  std::deque<std::unique_ptr<ServiceBase>> services_;

  Builder(const Builder& other) = delete;
  Builder& operator=(const Builder& other) = delete;
};

template <class S, class X, class Y>
class Server::Builder::Service final : public Server::ServiceBase {
 public:
  explicit Service(std::function<void(const X&, Y*)> handler)
      : handler_(std::move(handler)) {}

  void NewCallData(ServerCompletionQueue* cq) override {
    new CallData(this, cq);
  }

  typename S::AsyncService* get() { return &service_; }
  const std::function<void(const X&, Y*)>& handler() const { return handler_; }

 private:
  class CallData;

  typename S::AsyncService service_;
  const std::function<void(const X&, Y*)> handler_;
};

// Class encompasing the state and logic needed to serve a request.
template <class S, class X, class Y>
class Server::Builder::Service<S, X, Y>::CallData final
    : public Server::CallDataBase {
 public:
  // Take in the "service" instance (in this case representing an asynchronous
  // server) and the completion queue "cq" used for asynchronous communication
  // with the gRPC runtime.
  CallData(Service* service, ServerCompletionQueue* cq)
      : service_(service), cq_(cq) {
    // Invoke the serving logic right away.
    Proceed(true);
  }

  void Proceed(bool ok) override {
    if (!ok) status_ = CallStatus::FINISH;

    switch (status_) {
      case CallStatus::CREATE:
        // Make this instance progress to the PROCESS state.
        status_ = CallStatus::PROCESS;

        // As part of the initial CREATE state, we *request* that the system
        // start processing Process requests. In this request, "this" acts
        // are the tag uniquely identifying the request (so that different
        // CallData instances can serve different requests concurrently), in
        // this case the memory address of this CallData instance.
        service_->get()->RequestProcess(&ctx_, &request_, &responder_, cq_, cq_,
                                        this);
        break;
      case CallStatus::PROCESS:
        // Spawn a new CallData instance to serve new clients while we process
        // the one for this CallData. The instance will deallocate itself as
        // part of its FINISH state.
        new CallData(service_, cq_);

        // The actual processing.
        service_->handler()(request_, &reply_);

        // And we are done! Let the gRPC runtime know we've finished, using
        // the memory address of this instance as the uniquely identifying tag
        // for the event.
        status_ = CallStatus::FINISH;
        responder_.Finish(reply_, Status::OK, this);
        break;
      case CallStatus::FINISH:
        // Once in the FINISH state, deallocate ourselves (CallData).
        delete this;
        break;
    }
  }

 private:
  // The means of communication with the gRPC runtime for an asynchronous
  // server.
  Service* const service_;
  // The producer-consumer queue where for asynchronous server notifications.
  ServerCompletionQueue* const cq_;
  // Context for the rpc, allowing to tweak aspects of it such as the use
  // of compression, authentication, as well as to send metadata back to the
  // client.
  ServerContext ctx_;

  // What we get from the client.
  X request_;
  // What we send back to the client.
  Y reply_;

  // The means to get back to the client.
  ServerAsyncResponseWriter<Y> responder_{&ctx_};

  // Let's implement a tiny state machine with the following states.
  enum class CallStatus { CREATE, PROCESS, FINISH };
  CallStatus status_{CallStatus::CREATE};  // The current serving state.
};

}  // namespace unstructured
}  // namespace grpc

#endif  // GRPCXX_UNSTRUCTURED_H
