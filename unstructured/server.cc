#include <chrono>
#include <string>
#include <thread>

#include <grpc++/generic/async_generic_service.h>
#include <grpc++/grpc++.h>
#include <grpc++/unstructured.h>

#include "unstructured/unstructured.grpc.pb.h"
#include "utils.h"

namespace grpc {
namespace unstructured {

class TestService final : public Test::Service {
 public:
  Status Process(ServerContext* context, const TestRequest* request,
                 TestReply* reply) override {
    reply->set_output(7 + request->input());
    return Status::OK;
  }
};

class UnstructuredService final : public Unstructured::Service {
 public:
  Status Process(ServerContext* context, const UnstructuredRequest* request,
                 UnstructuredReply* reply) override {
    reply->set_output("Hello " + request->input());
    return Status::OK;
  }
};

}  // namespace unstructured
}  // namespace grpc

int main() {
  grpc::SslServerCredentialsOptions ssco;
  ssco.pem_root_certs = unstructured::ReadFile("unstructured/keys/root-cert.pem");
  ssco.pem_key_cert_pairs.push_back(
      {unstructured::ReadFile("unstructured/keys/a-key.pem"),
       unstructured::ReadFile("unstructured/keys/a-cert.pem")});
  grpc::unstructured::TestService test_service;
  grpc::unstructured::UnstructuredService unstructured_service;
  grpc::AsyncGenericService ags;
  auto server = grpc::unstructured::Server::Builder()
      .AddListeningPort("0.0.0.0:50051", SslServerCredentials(ssco))
      .RegisterService(&test_service)
      .RegisterService(&unstructured_service)
      .RegisterAsyncGenericService(&ags)
      .BuildAndStart();
  std::this_thread::sleep_for(std::chrono::seconds(60));
  return 0;
}
