#include <grpc++/unstructured.h>

#include <memory>
#include <string>
#include <thread>

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>

#include "unstructured/unstructured.grpc.pb.h"

namespace unstructured {

class Server::Impl final {
 public:
  ~Impl() {
    server_->Shutdown();
    cq_->Shutdown();
    gpr_log(GPR_ERROR, "Server shutting down");
    handle_rpcs_thread_.join();
  }

  // There is no shutdown handling in this code.
  void Run(std::string address, const grpc::SslServerCredentialsOptions& ssco) {
    grpc::ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(address, grpc::SslServerCredentials(ssco));
    // Register "service_" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    builder.RegisterService(&service_);
    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.
    cq_ = builder.AddCompletionQueue();
    // Finally assemble the server.
    server_ = builder.BuildAndStart();
    gpr_log(GPR_ERROR, "Server listening on %s", address.c_str());

    handle_rpcs_thread_ = std::thread([this] { HandleRpcs(); });
  }

 private:
  // Class encompasing the state and logic needed to serve a request.
  class CallData {
   public:
    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    CallData(Unstructured::AsyncService* service,
             grpc::ServerCompletionQueue* cq) : service_(service), cq_(cq) {
      // Invoke the serving logic right away.
      Proceed(true);
    }

    void Proceed(bool ok) {
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
          service_->RequestProcess(&ctx_, &request_, &responder_, cq_, cq_,
                                   this);
          break;
        case CallStatus::PROCESS:
          // Spawn a new CallData instance to serve new clients while we process
          // the one for this CallData. The instance will deallocate itself as
          // part of its FINISH state.
          new CallData(service_, cq_);

          // The actual processing.
          reply_.set_output("Hello " + request_.input());

          // And we are done! Let the gRPC runtime know we've finished, using
          // the memory address of this instance as the uniquely identifying tag
          // for the event.
          status_ = CallStatus::FINISH;
          responder_.Finish(reply_, grpc::Status::OK, this);
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
    Unstructured::AsyncService* const service_;
    // The producer-consumer queue where for asynchronous server notifications.
    grpc::ServerCompletionQueue* const cq_;
    // Context for the rpc, allowing to tweak aspects of it such as the use
    // of compression, authentication, as well as to send metadata back to the
    // client.
    grpc::ServerContext ctx_;

    // What we get from the client.
    UnstructuredRequest request_;
    // What we send back to the client.
    UnstructuredReply reply_;

    // The means to get back to the client.
    grpc::ServerAsyncResponseWriter<UnstructuredReply> responder_{&ctx_};

    // Let's implement a tiny state machine with the following states.
    enum class CallStatus { CREATE, PROCESS, FINISH };
    CallStatus status_{CallStatus::CREATE};  // The current serving state.
  };

  // This can be run in multiple threads if needed.
  void HandleRpcs() {
    // Spawn a new CallData instance to serve new clients.
    new CallData(&service_, cq_.get());
    void* tag;  // uniquely identifies a request.
    bool ok;
    // Block waiting to read the next event from the completion queue. The
    // event is uniquely identified by its tag, which in this case is the
    // memory address of a CallData instance.
    // The return value of Next should always be checked. This return value
    // tells us whether there is any kind of event or cq_ is shutting down.
    while (cq_->Next(&tag, &ok)) {
      static_cast<CallData*>(tag)->Proceed(ok);
    }
  }

  std::unique_ptr<grpc::ServerCompletionQueue> cq_;
  Unstructured::AsyncService service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread handle_rpcs_thread_;
};

Server::Server(std::string address,
               const grpc::SslServerCredentialsOptions& ssco,
               std::function<std::string(std::string input)> rpc_cb,
               std::function<std::string(const std::string& host,
                                         const std::string& method,
                                         std::string input,
                                         const char** content_type)> browser_cb)
    : impl_(new Impl) {
  impl_->Run(std::move(address), ssco);
}

Server::~Server() {}

}  // namespace unstructured
