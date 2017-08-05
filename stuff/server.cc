#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>

#include "utils.h"
#include "stuff/helloworld.grpc.pb.h"

namespace stuff {

class ServerImpl final {
 public:
  ~ServerImpl() {
    gpr_log(GPR_ERROR, "Server shutting down");
    shutdown_thread_.join();
  }

  // There is no shutdown handling in this code.
  void Run() {
    std::string server_address("0.0.0.0:50051");

    grpc::ServerBuilder builder;
    grpc::SslServerCredentialsOptions ssco;
    ssco.pem_root_certs = ReadFile("stuff/keys/root-cert.pem");
    ssco.pem_key_cert_pairs.push_back({ReadFile("stuff/keys/a-key.pem"),
                                       ReadFile("stuff/keys/a-cert.pem")});
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::SslServerCredentials(ssco));
    // Register "service_" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    builder.RegisterService(&service_);
    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.
    cq_ = builder.AddCompletionQueue();
    // Finally assemble the server.
    server_ = builder.BuildAndStart();
    gpr_log(GPR_ERROR, "Server listening on %s", server_address.c_str());

    shutdown_thread_ = std::thread([this] {
      std::this_thread::sleep_for(std::chrono::seconds(60));
      server_->Shutdown();
      cq_->Shutdown();
    });
    // Proceed to the server's main loop.
    HandleRpcs();
  }

 private:
  // Class encompasing the state and logic needed to serve a request.
  class CallData {
   public:
    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    CallData(helloworld::Greeter::AsyncService* service,
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
          // start processing SayHello requests. In this request, "this" acts
          // are the tag uniquely identifying the request (so that different
          // CallData instances can serve different requests concurrently), in
          // this case the memory address of this CallData instance.
          service_->RequestSayHello(&ctx_, &request_, &responder_, cq_, cq_,
                                    this);
          break;
        case CallStatus::PROCESS:
          // Spawn a new CallData instance to serve new clients while we process
          // the one for this CallData. The instance will deallocate itself as
          // part of its FINISH state.
          new CallData(service_, cq_);

          // The actual processing.
          reply_.set_message("Hello " + request_.name());

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
    helloworld::Greeter::AsyncService* const service_;
    // The producer-consumer queue where for asynchronous server notifications.
    grpc::ServerCompletionQueue* const cq_;
    // Context for the rpc, allowing to tweak aspects of it such as the use
    // of compression, authentication, as well as to send metadata back to the
    // client.
    grpc::ServerContext ctx_;

    // What we get from the client.
    helloworld::HelloRequest request_;
    // What we send back to the client.
    helloworld::HelloReply reply_;

    // The means to get back to the client.
    grpc::ServerAsyncResponseWriter<helloworld::HelloReply> responder_{&ctx_};

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
  helloworld::Greeter::AsyncService service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread shutdown_thread_;
};

}  // namespace stuff

int main() {
  stuff::ServerImpl server;
  server.Run();
  return 0;
}
