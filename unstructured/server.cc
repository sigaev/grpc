#include <chrono>
#include <string>
#include <thread>

#include <grpc++/grpc++.h>
#include <grpc++/unstructured.h>

#include "utils.h"

int main() {
  grpc::SslServerCredentialsOptions ssco;
  ssco.pem_root_certs = unstructured::ReadFile("unstructured/keys/root-cert.pem");
  ssco.pem_key_cert_pairs.push_back(
      {unstructured::ReadFile("unstructured/keys/a-key.pem"),
       unstructured::ReadFile("unstructured/keys/a-cert.pem")});
  unstructured::Server s(
      "0.0.0.0:50051",
      ssco,
      [] (std::string input) { return input; },
      [] (const std::string& host,
          const std::string& method,
          std::string input,
          const char** content_type) {
        *content_type = "text/html";
        return input;
      });
  std::this_thread::sleep_for(std::chrono::seconds(60));
  return 0;
}
