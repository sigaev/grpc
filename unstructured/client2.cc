#include <atomic>
#include <iostream>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <time.h>
#if _WINDOWS
#include <windows.h>
#endif

#include "unstructured/unstructured.grpc.pb.h"
#include "utils.h"

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using grpc::sync_over_async::UnstructuredRequest;
using grpc::sync_over_async::UnstructuredReply;
using grpc::sync_over_async::Unstructured;

std::atomic<uint64_t> g_num_total(0);
std::atomic<int> g_max_num_pending(105);

class UnstructuredClient {
 public:
  explicit UnstructuredClient(std::shared_ptr<Channel> channel)
      : stub_(Unstructured::NewStub(channel)) {}

  // Assembles the client's payload and sends it to the server.
  void Process(const std::string& user) {
    {
      std::unique_lock<std::mutex> ul(m_);
      while (num_pending_ >=
             g_max_num_pending.load(std::memory_order_relaxed)) {
        cv_.wait(ul);
      }
      ++num_pending_;
    }
    g_num_total.fetch_add(1, std::memory_order_relaxed);

    // Data we are sending to the server.
    UnstructuredRequest request;
    request.set_input(user);

    // Call object to store rpc data
    auto* call = new AsyncClientCall;

    // stub_->AsyncProcess() performs the RPC call, returning an instance to
    // store in "call". Because we are using the asynchronous API, we need to
    // hold on to the "call" instance in order to get updates on the ongoing
    // RPC.
    call->response_reader =
        stub_->AsyncProcess(&call->context, request, &cq_);


    // Request that, upon completion of the RPC, "reply" be updated with the
    // server's response; "status" with the indication of whether the operation
    // was successful. Tag the request with the memory address of the call
    // object.
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
  }

  // Loop while listening for completed responses.
  // Prints out the response from the server.
  void AsyncCompleteRpc() {
    void* got_tag;
    bool ok = false;

    // Block until the next result is available in the completion queue "cq".
    while (cq_.Next(&got_tag, &ok)) {
      // The tag in this example is the memory location of the call object
      AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

      // Verify that the request was completed successfully. Note that "ok"
      // corresponds solely to the request for updates introduced by Finish().
      GPR_ASSERT(ok);

      GPR_ASSERT(call->status.ok());

      // Once we're complete, deallocate the call object.
      delete call;
      m_.lock();
      int num_pending = --num_pending_;
      m_.unlock();
      if (num_pending < g_max_num_pending.load(std::memory_order_relaxed)) {
        cv_.notify_one();
      }
    }
  }

 private:
  // struct for keeping state and data information
  struct AsyncClientCall {
    // Container for the data we expect from the server.
    UnstructuredReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // Storage for the status of the RPC upon completion.
    Status status;

    std::unique_ptr<ClientAsyncResponseReader<UnstructuredReply>> response_reader;
  };

  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<Unstructured::Stub> stub_;

  // The producer-consumer queue we use to communicate asynchronously with the
  // gRPC runtime.
  CompletionQueue cq_;
  int num_pending_ = 0;
  std::mutex m_;
  std::condition_variable cv_;
};

int main() {
  std::cout << "Press control-c to quit. QPS:" << std::endl;
  auto f = [] {
    // Instantiate the client. It requires a channel, out of which the actual
    // RPCs are created. This channel models a connection to an endpoint (in
    // this case, localhost at port 50051).
    UnstructuredClient uc(grpc::CreateChannel(
        "localhost:50051",
        grpc::SslCredentials(
            {unstructured::ReadFile("unstructured/keys/root-cert.pem"),
             "",
             ""})));

    // Spawn reader thread that loops indefinitely
    std::thread t(&UnstructuredClient::AsyncCompleteRpc, &uc);
    for (unsigned i = 0; ; ++i) {
      std::string user("world " + std::to_string(i));
      uc.Process(user);  // The actual RPC call!
    }
  };
  std::thread t0(f), t1(f);
  uint64_t ns0 = 0;
  uint64_t num_total0 = 0;
  for (;;) {
#if _WINDOWS
    LARGE_INTEGER count, freq;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&freq);
    const uint64_t ns1 = count.QuadPart;
    const double scale = freq.QuadPart;
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t ns1 = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    const double scale = 1e9;
#endif
    const uint64_t num_total1 = g_num_total.load(std::memory_order_relaxed);
    if (ns0 != 0) {
      std::cout << (num_total1 - num_total0) * scale / (ns1 - ns0) << std::endl;
    }
    ns0 = ns1;
    num_total0 = num_total1;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return 0;
}
