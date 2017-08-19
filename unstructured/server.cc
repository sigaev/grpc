#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <grpc++/ext/sync_over_async_plugin.h>
#include <grpc++/generic/async_generic_service.h>
#include <grpc++/grpc++.h>

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

class CallData final : public CallDataBase {
 public:
  CallData(AsyncGenericService* service, ServerCompletionQueue* cq)
      : generic_service_(service), cq_(cq) {
    generic_service_->RequestCall(&ctx_, &stream_, cq_, cq_, this);
  }

  void Proceed(bool ok) override {
    if (!ok) status_ = CallStatus::FINISH;

    switch (status_) {
      case CallStatus::PROCESS: {
        new CallData(generic_service_, cq_);

        ctx_.SetHTML();

        static std::atomic<int> count(0);
        static constexpr int kSize = 1024;
        std::unique_ptr<char[]> chars(new char[kSize]);
        const size_t len = std::max(0, std::min(kSize - 1,
            snprintf(chars.get(), kSize,
"<html><head><link rel=icon href=\"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAA"
"AABJRU5ErkJggg==\"></head>"
"<body>This <b>is</b> Навуходоносор Второй. 小米科技. Method: %s. Count: %d.</body></html>",
                     ctx_.method().c_str(), count++)));
        Slice s(SliceFromCharArray(std::move(chars), len), Slice::STEAL_REF);

        stream_.WriteAndFinish(ByteBuffer(&s, 1), WriteOptions().set_raw(),
                               Status::OK, this);
        status_ = CallStatus::FINISH;
        break;
      }
      case CallStatus::FINISH: {
        delete this;
        break;
      }
    }
  }

 private:
  AsyncGenericService* const generic_service_;
  ServerCompletionQueue* const cq_;
  GenericServerContext ctx_;
  GenericServerAsyncReaderWriter stream_{&ctx_};
  enum class CallStatus { PROCESS, FINISH };
  CallStatus status_{CallStatus::PROCESS};
};

}  // namespace unstructured
}  // namespace grpc

static constexpr bool kAsync = true;
static constexpr bool kGeneric = true;

int main() {
  using namespace grpc;
  if (kAsync) {
    ServerBuilder::InternalAddPluginFactory([] {
      return std::unique_ptr<ServerBuilderPlugin>(new SyncOverAsyncPlugin);
    });
    if (kGeneric) {
      SyncOverAsyncPlugin::SetGenericCallDataFactory(
          [] (AsyncGenericService* generic_service, ServerCompletionQueue* cq) {
            new grpc::unstructured::CallData(generic_service, cq);
          });
    }
  }

  SslServerCredentialsOptions ssco;
  ssco.pem_root_certs =
      ::unstructured::ReadFile("unstructured/keys/root-cert.pem");
  ssco.pem_key_cert_pairs.push_back(
      {::unstructured::ReadFile("unstructured/keys/a-key.pem"),
       ::unstructured::ReadFile("unstructured/keys/a-cert.pem")});
  grpc::unstructured::TestService test_service;
  grpc::unstructured::UnstructuredService unstructured_service;
  AsyncGenericService ags;
  ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:50051", SslServerCredentials(ssco))
         .RegisterService(&test_service)
         .RegisterService(&unstructured_service);
  if (kAsync && kGeneric) builder.RegisterAsyncGenericService(&ags);
  std::unique_ptr<ServerCompletionQueue> cq;
  if (kAsync) cq = builder.AddCompletionQueue();
  auto server = builder.BuildAndStart();
  std::this_thread::sleep_for(std::chrono::seconds(30));
  return 0;
}
