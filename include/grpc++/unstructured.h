#ifndef GRPCXX_UNSTRUCTURED_H
#define GRPCXX_UNSTRUCTURED_H

#include <functional>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

namespace unstructured {

class Server {
 public:
  Server(std::string address,
         const grpc::SslServerCredentialsOptions& ssco,
         std::function<std::string(std::string input)> rpc_cb,
         std::function<std::string(const std::string& host,
                                   const std::string& method,
                                   std::string input,
                                   const char** content_type)> browser_cb);
  ~Server();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  Server(const Server& other) = delete;
  Server& operator=(const Server& other) = delete;
};

}  // namespace unstructured

#endif  // GRPCXX_UNSTRUCTURED_H
