#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>

#include <grpc++/ext/sync_over_async_plugin.h>
#include <grpc++/generic/async_generic_service.h>
#include <grpc++/grpc++.h>

#include "unstructured/unstructured.grpc.pb.h"
#include "utils.h"

namespace grpc {
namespace sync_over_async {

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
#if 0
    volatile int a = 1e5;
    while (a-- > 0) {}
#endif
    return Status::OK;
  }
};

class CallData;

class Fan final {
 public:
  void Add(CallData* call);
  void Publish(std::unique_ptr<char[]> chars, size_t len);

  bool IsShutdown() const {
    std::lock_guard<std::mutex> lg(mu_);
    return shutdown_;
  }

  void Shutdown() {
    std::lock_guard<std::mutex> lg(mu_);
    shutdown_ = true;
  }

 private:
  std::deque<CallData*> calls_;
  bool shutdown_ = false;
  mutable std::mutex mu_;
};

class CallData final : public CallDataBase {
 public:
  CallData(Fan* fan, AsyncGenericService* service, ServerCompletionQueue* cq)
      : fan_(fan), generic_service_(service), cq_(cq) {
    generic_service_->RequestCall(&ctx_, &stream_, cq_, cq_, this);
  }

  void ProceedWithMessage(const Slice& slice, int64_t publish_time) {
    static const string s("\n\n");
    slices_[0] = slice;
    slices_[2] = Slice(SliceReferencingString(s), Slice::STEAL_REF);
    publish_time_ = publish_time;
    Proceed(true);
  }

 private:
  void Proceed(bool ok) override {
    if ((count_ *= ok)-- == 0) {
      delete this;
      return;
    }

    if (count_ == 0) {
      new CallData(fan_, generic_service_, cq_);
      static const char kStream[] = "/stream";
      if (memcmp(ctx_.method().data(), kStream, sizeof(kStream) - 1) == 0) {
        ctx_.SetContentType("text/event-stream; charset=UTF-8");
        count_ = -1;
      } else {
        ctx_.SetContentType("text/html; charset=UTF-8");
      }
      GPR_ASSERT(creation_time_ == 0);
      creation_time_ = Now();
    }
    if (fan_->IsShutdown()) count_ = 0;
    if ((count_ & 1) != 0) {
      fan_->Add(this);
    } else {
      static constexpr int kSize = 1024;
      if (slices_[0].size() == 0) {
        GPR_ASSERT(count_ == 0);
        static std::atomic<int> count(0);
        std::unique_ptr<char[]> chars(new char[kSize]);
        const size_t len = std::max(
            0,
            std::min(
                kSize - 1,
                snprintf(
                    chars.get(), kSize,
                    "<html><head><link rel=icon href=\"data:image/png;base64,"
                    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4n"
                    "GMAAQAABQABDQottAAAAABJRU5ErkJggg==\"></head>"
                    "<body>This <b>is</b> Навуходоносор Второй. 小米科技.<br>"
                    "Server stream:<pre id=stream>&nbsp;</pre><script>"
                    "var elem = document.getElementById('stream');"
                    "var src = new EventSource('/stream');"
                    "src.onmessage = "
                    "function(event) { elem.textContent = event.data; }"
                    "</script>Method: %s. Count: %d. Publish time, latency: ",
                    ctx_.method().c_str(), count++)));
        static const string s("</body></html>\n");
        slices_[0] =
            Slice(SliceFromCharArray(std::move(chars), len), Slice::STEAL_REF);
        slices_[2] = Slice(SliceReferencingString(s), Slice::STEAL_REF);
        publish_time_ = Now();
      }
      std::unique_ptr<char[]> chars(new char[kSize]);
      const size_t len = std::max(
          0,
          std::min(kSize - 1, snprintf(chars.get(), kSize, "% 8.3f s % 8.1f µs",
                                       (publish_time_ - creation_time_) * 1e-9,
                                       (Now() - publish_time_) * 1e-3)));
      slices_[1] =
          Slice(SliceFromCharArray(std::move(chars), len), Slice::STEAL_REF);
      if (count_ != 0) {
        stream_.Write(ByteBuffer(slices_, 3), WriteOptions().set_raw(), this);
      } else {
        stream_.WriteAndFinish(ByteBuffer(slices_, 3), WriteOptions().set_raw(),
                               Status::OK, this);
      }
    }
  }

  Fan* const fan_;
  AsyncGenericService* const generic_service_;
  ServerCompletionQueue* const cq_;
  GenericServerContext ctx_;
  GenericServerAsyncReaderWriter stream_{&ctx_};
  Slice slices_[3];
  int64_t publish_time_;
  int64_t creation_time_ = 0;
  int count_ = 1;
};

void Fan::Add(CallData* call) {
  mu_.lock();
  if (shutdown_) {
    mu_.unlock();
    static const string s("data: ! ");
    Slice slice(SliceReferencingString(s), Slice::STEAL_REF);
    call->ProceedWithMessage(slice, Now());
  } else {
    calls_.push_back(call);
    mu_.unlock();
  }
}

void Fan::Publish(std::unique_ptr<char[]> chars, size_t len) {
  const int64_t now = Now();
  std::deque<CallData*> calls;
  mu_.lock();
  calls = std::move(calls_);
  calls_.clear();
  mu_.unlock();
  Slice slice(SliceFromCharArray(std::move(chars), len), Slice::STEAL_REF);
  for (auto* call : calls) {
    call->ProceedWithMessage(slice, now);
  }
}

}  // namespace sync_over_async
}  // namespace grpc

static constexpr bool kAsync = true;
static constexpr bool kGeneric = true;
static_assert(kAsync || !kGeneric, "Generic requires async");

int main() {
  using namespace grpc;
  static sync_over_async::Fan fan;
  if (kAsync) {
    ServerBuilder::InternalAddPluginFactory([] {
      std::function<void(AsyncGenericService*, ServerCompletionQueue*)> factory;
      if (kGeneric) {
        factory = [](AsyncGenericService* generic_service,
                     ServerCompletionQueue* cq) {
          new sync_over_async::CallData(&fan, generic_service, cq);
        };
      }
      return std::unique_ptr<ServerBuilderPlugin>(
          new SyncOverAsyncPlugin(std::move(factory)));
    });
  }

  SslServerCredentialsOptions ssco;
  ssco.pem_root_certs =
      unstructured::ReadFile("unstructured/keys/root-cert.pem");
  ssco.pem_key_cert_pairs.push_back(
      {unstructured::ReadFile("unstructured/keys/a-key.pem"),
       unstructured::ReadFile("unstructured/keys/a-cert.pem")});
  sync_over_async::TestService test_service;
  sync_over_async::UnstructuredService unstructured_service;
  AsyncGenericService ags;
  ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:50051", SslServerCredentials(ssco))
      .RegisterService(&test_service)
      .RegisterService(&unstructured_service);
  if (kGeneric) builder.RegisterAsyncGenericService(&ags);
  std::unique_ptr<ServerCompletionQueue> cq;
  if (kAsync) cq = builder.AddCompletionQueue();
  auto server = builder.BuildAndStart();
  for (int i = 0; i < 1600; ++i) {
    std::unique_ptr<char[]> chars(new char[8]);
    memcpy(chars.get(), "data: \0 ", 8);
    chars[6] = 'A' + (i & 31);
    fan.Publish(std::move(chars), 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  std::unique_ptr<char[]> chars(new char[8]);
  memcpy(chars.get(), "data: - ", 8);
  fan.Shutdown();
  fan.Publish(std::move(chars), 8);
  return 0;
}
