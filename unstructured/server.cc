#include <chrono>
#include <string>
#include <thread>

#include <grpc++/grpc++.h>
#include <grpc++/unstructured.h>

#include "unstructured/unstructured.grpc.pb.h"
#include "utils.h"

int main() {
  grpc::SslServerCredentialsOptions ssco;
  ssco.pem_root_certs = unstructured::ReadFile("unstructured/keys/root-cert.pem");
  ssco.pem_key_cert_pairs.push_back(
      {unstructured::ReadFile("unstructured/keys/a-key.pem"),
       unstructured::ReadFile("unstructured/keys/a-cert.pem")});
  auto server = grpc::unstructured::Server::Builder()
      .AddListeningPort("0.0.0.0:50051", SslServerCredentials(ssco))
      .AddService<grpc::unstructured::Test,
                  grpc::unstructured::TestRequest,
                  grpc::unstructured::TestReply>(
          [] (const grpc::unstructured::TestRequest& request,
              grpc::unstructured::TestReply* reply) {
            reply->set_output(13 + 2 * request.input());
          })
      .AddService<grpc::unstructured::Unstructured,
                  grpc::unstructured::UnstructuredRequest,
                  grpc::unstructured::UnstructuredReply>(
          [] (const grpc::unstructured::UnstructuredRequest& request,
              grpc::unstructured::UnstructuredReply* reply) {
            reply->set_output("Hello " + request.input());
          })
      .BuildAndStart();
  std::this_thread::sleep_for(std::chrono::seconds(60));
  return 0;
}
