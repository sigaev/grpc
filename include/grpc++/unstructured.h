#ifndef GRPCXX_UNSTRUCTURED_H
#define GRPCXX_UNSTRUCTURED_H

#include <memory>
#include <string>
#include <vector>

#include <grpc++/grpc++.h>

namespace grpc {
namespace unstructured {

class Server final {
 public:
  class Builder;

  Server();
  Server(Server&& other);
  ~Server();

 private:
  class Impl;
  struct Handler;
  struct ServerWithFriends;

  explicit Server(ServerWithFriends sws);

  std::unique_ptr<Impl> impl_;

  Server(const Server& other) = delete;
  Server& operator=(const Server& other) = delete;
};

class Server::Builder final {
 public:
  Builder();
  ~Builder();

  Builder& AddListeningPort(const std::string& addr,
                            std::shared_ptr<ServerCredentials> creds,
                            int* selected_port = nullptr);
  Builder& RegisterService(Service* service);
  Builder& RegisterAsyncGenericService(AsyncGenericService* service);
  Server BuildAndStart();

 private:
  ServerBuilder builder_;
  std::vector<Handler> handlers_;
  AsyncGenericService* generic_service_ = nullptr;

  Builder(const Builder& other) = delete;
  Builder& operator=(const Builder& other) = delete;
};

}  // namespace unstructured
}  // namespace grpc

#endif  // GRPCXX_UNSTRUCTURED_H
