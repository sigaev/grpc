#include <iostream>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

#include "utils.h"
#include "unstructured/unstructured.grpc.pb.h"

class UnstructuredClient {
 public:
  UnstructuredClient(std::shared_ptr<grpc::Channel> channel)
      : stub_(grpc::unstructured::Test::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  int Process(const std::string& user) {
    // Data we are sending to the server.
    grpc::unstructured::TestRequest request;
    request.set_input(13);

    // Container for the data we expect from the server.
    grpc::unstructured::TestReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    grpc::ClientContext context;

    // The actual RPC.
    grpc::Status status = stub_->Process(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
      return reply.output();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return -1;
    }
  }

 private:
  std::unique_ptr<grpc::unstructured::Test::Stub> stub_;
};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051). We indicate that the channel isn't authenticated
  // (use of InsecureChannelCredentials()).
  UnstructuredClient uc(grpc::CreateChannel(
      "localhost:50051",
      grpc::SslCredentials(
          {unstructured::ReadFile("unstructured/keys/root-cert.pem"),
           "",
           ""})));
  std::string user("world");
  auto reply = uc.Process(user);
  std::cout << "Unstructured received: " << reply << std::endl;

  return 0;
}
