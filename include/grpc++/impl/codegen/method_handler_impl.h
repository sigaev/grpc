/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPCXX_IMPL_CODEGEN_METHOD_HANDLER_IMPL_H
#define GRPCXX_IMPL_CODEGEN_METHOD_HANDLER_IMPL_H

#include <grpc++/impl/codegen/async_unary_call.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/rpc_service_method.h>
#include <grpc++/impl/codegen/sync_stream.h>

namespace grpc {

/// A wrapper class of an application provided rpc method handler.
template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler : public MethodHandler {
 public:
  RpcMethodHandler(std::function<Status(ServiceType*, ServerContext*,
                                        const RequestType*, ResponseType*)>
                       func,
                   ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    RequestType req;
    Status status =
        SerializationTraits<RequestType>::Deserialize(param.request, &req);
    ResponseType rsp;
    if (status.ok()) {
      status = func_(service_, param.server_context, &req, &rsp);
    }

    GPR_CODEGEN_ASSERT(!param.server_context->sent_initial_metadata_);
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        ops;
    ops.SendInitialMetadata(param.server_context->initial_metadata_,
                            param.server_context->initial_metadata_flags());
    if (param.server_context->compression_level_set()) {
      ops.set_compression_level(param.server_context->compression_level());
    }
    if (status.ok()) {
      status = ops.SendMessage(rsp);
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  class CallData;
  void NewCallData(ServerCompletionQueue* cq, size_t idx) final;

  /// Application provided rpc handler function.
  std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                       ResponseType*)>
      func_;
  // The class the above handler function lives in.
  ServiceType* service_;
};

namespace unstructured {

class CallDataBase {
 public:
  virtual ~CallDataBase() {}
  virtual void Proceed(bool ok) = 0;
};

}  // namespace unstructured

template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler<ServiceType, RequestType, ResponseType>::CallData final
    : public unstructured::CallDataBase {
 public:
  // Take in the "service" instance (in this case representing an asynchronous
  // server) and the completion queue "cq" used for asynchronous communication
  // with the gRPC runtime.
  CallData(RpcMethodHandler* handler, ServerCompletionQueue* cq, size_t idx)
      : handler_(handler), cq_(cq), idx_(idx) {
    // Invoke the serving logic right away.
    Proceed(true);
  }

  void Proceed(bool ok) override {
    if (!ok) status_ = CallStatus::FINISH;

    switch (status_) {
      case CallStatus::CREATE: {
        // Make this instance progress to the PROCESS state.
        status_ = CallStatus::PROCESS;

        // As part of the initial CREATE state, we *request* that the system
        // start processing Process requests. In this request, "this" acts
        // are the tag uniquely identifying the request (so that different
        // CallData instances can serve different requests concurrently), in
        // this case the memory address of this CallData instance.
        handler_->service_->RequestAsyncUnary(
            idx_, &ctx_, &request_, &responder_, cq_, cq_, this);
        break;
      }
      case CallStatus::PROCESS: {
        // Spawn a new CallData instance to serve new clients while we process
        // the one for this CallData. The instance will deallocate itself as
        // part of its FINISH state.
        new CallData(handler_, cq_, idx_);

        // The actual processing.
        Status status =
            handler_->func_(handler_->service_, &ctx_, &request_, &reply_);

        // And we are done! Let the gRPC runtime know we've finished, using
        // the memory address of this instance as the uniquely identifying tag
        // for the event.
        status_ = CallStatus::FINISH;
        responder_.Finish(reply_, status, this);
        break;
      }
      case CallStatus::FINISH: {
        // Once in the FINISH state, deallocate ourselves (CallData).
        delete this;
        break;
      }
    }
  }

 private:
  // The means of communication with the gRPC runtime for an asynchronous
  // server.
  RpcMethodHandler* const handler_;
  // The producer-consumer queue where for asynchronous server notifications.
  ServerCompletionQueue* const cq_;
  const size_t idx_;
  // Context for the rpc, allowing to tweak aspects of it such as the use
  // of compression, authentication, as well as to send metadata back to the
  // client.
  ServerContext ctx_;

  // What we get from the client.
  RequestType request_;
  // What we send back to the client.
  ResponseType reply_;

  // The means to get back to the client.
  ServerAsyncResponseWriter<ResponseType> responder_{&ctx_};

  // Let's implement a tiny state machine with the following states.
  enum class CallStatus { CREATE, PROCESS, FINISH };
  CallStatus status_{CallStatus::CREATE};  // The current serving state.
};

template <class ServiceType, class RequestType, class ResponseType>
void RpcMethodHandler<ServiceType, RequestType, ResponseType>::NewCallData(
    ServerCompletionQueue* cq, size_t idx) {
  new CallData(this, cq, idx);
}

/// A wrapper class of an application provided client streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ClientStreamingHandler : public MethodHandler {
 public:
  ClientStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*,
                           ServerReader<RequestType>*, ResponseType*)>
          func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    ServerReader<RequestType> reader(param.call, param.server_context);
    ResponseType rsp;
    Status status = func_(service_, param.server_context, &reader, &rsp);

    GPR_CODEGEN_ASSERT(!param.server_context->sent_initial_metadata_);
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage,
              CallOpServerSendStatus>
        ops;
    ops.SendInitialMetadata(param.server_context->initial_metadata_,
                            param.server_context->initial_metadata_flags());
    if (param.server_context->compression_level_set()) {
      ops.set_compression_level(param.server_context->compression_level());
    }
    if (status.ok()) {
      status = ops.SendMessage(rsp);
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServiceType*, ServerContext*, ServerReader<RequestType>*,
                       ResponseType*)>
      func_;
  ServiceType* service_;
};

/// A wrapper class of an application provided server streaming handler.
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler : public MethodHandler {
 public:
  ServerStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                           ServerWriter<ResponseType>*)>
          func,
      ServiceType* service)
      : func_(func), service_(service) {}

  void RunHandler(const HandlerParameter& param) final {
    RequestType req;
    Status status =
        SerializationTraits<RequestType>::Deserialize(param.request, &req);

    if (status.ok()) {
      ServerWriter<ResponseType> writer(param.call, param.server_context);
      status = func_(service_, param.server_context, &req, &writer);
    }

    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServiceType*, ServerContext*, const RequestType*,
                       ServerWriter<ResponseType>*)>
      func_;
  ServiceType* service_;
};

/// A wrapper class of an application provided bidi-streaming handler.
/// This also applies to server-streamed implementation of a unary method
/// with the additional requirement that such methods must have done a
/// write for status to be ok
/// Since this is used by more than 1 class, the service is not passed in.
/// Instead, it is expected to be an implicitly-captured argument of func
/// (through bind or something along those lines)
template <class Streamer, bool WriteNeeded>
class TemplatedBidiStreamingHandler : public MethodHandler {
 public:
  TemplatedBidiStreamingHandler(
      std::function<Status(ServerContext*, Streamer*)> func)
      : func_(func), write_needed_(WriteNeeded) {}

  void RunHandler(const HandlerParameter& param) final {
    Streamer stream(param.call, param.server_context);
    Status status = func_(param.server_context, &stream);

    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    if (!param.server_context->sent_initial_metadata_) {
      ops.SendInitialMetadata(param.server_context->initial_metadata_,
                              param.server_context->initial_metadata_flags());
      if (param.server_context->compression_level_set()) {
        ops.set_compression_level(param.server_context->compression_level());
      }
      if (write_needed_ && status.ok()) {
        // If we needed a write but never did one, we need to mark the
        // status as a fail
        status = Status(StatusCode::INTERNAL,
                        "Service did not provide response message");
      }
    }
    ops.ServerSendStatus(param.server_context->trailing_metadata_, status);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }

 private:
  std::function<Status(ServerContext*, Streamer*)> func_;
  const bool write_needed_;
};

template <class ServiceType, class RequestType, class ResponseType>
class BidiStreamingHandler
    : public TemplatedBidiStreamingHandler<
          ServerReaderWriter<ResponseType, RequestType>, false> {
 public:
  BidiStreamingHandler(
      std::function<Status(ServiceType*, ServerContext*,
                           ServerReaderWriter<ResponseType, RequestType>*)>
          func,
      ServiceType* service)
      : TemplatedBidiStreamingHandler<
            ServerReaderWriter<ResponseType, RequestType>, false>(std::bind(
            func, service, std::placeholders::_1, std::placeholders::_2)) {}
};

template <class RequestType, class ResponseType>
class StreamedUnaryHandler
    : public TemplatedBidiStreamingHandler<
          ServerUnaryStreamer<RequestType, ResponseType>, true> {
 public:
  explicit StreamedUnaryHandler(
      std::function<Status(ServerContext*,
                           ServerUnaryStreamer<RequestType, ResponseType>*)>
          func)
      : TemplatedBidiStreamingHandler<
            ServerUnaryStreamer<RequestType, ResponseType>, true>(func) {}
};

template <class RequestType, class ResponseType>
class SplitServerStreamingHandler
    : public TemplatedBidiStreamingHandler<
          ServerSplitStreamer<RequestType, ResponseType>, false> {
 public:
  explicit SplitServerStreamingHandler(
      std::function<Status(ServerContext*,
                           ServerSplitStreamer<RequestType, ResponseType>*)>
          func)
      : TemplatedBidiStreamingHandler<
            ServerSplitStreamer<RequestType, ResponseType>, false>(func) {}
};

/// Handle unknown method by returning UNIMPLEMENTED error.
class UnknownMethodHandler : public MethodHandler {
 public:
  static void FillOps(ServerContext* context,
                      CallOpSet<CallOpSendInitialMetadata,
                                CallOpSendMessage,
                                CallOpServerSendStatus>* ops);

  static void FillOps(ServerContext* context,
                      CallOpSet<CallOpSendInitialMetadata,
                                CallOpServerSendStatus>* ops) {
    Status status(StatusCode::UNIMPLEMENTED, "");
    if (!context->sent_initial_metadata_) {
      ops->SendInitialMetadata(context->initial_metadata_,
                               context->initial_metadata_flags());
      if (context->compression_level_set()) {
        ops->set_compression_level(context->compression_level());
      }
      context->sent_initial_metadata_ = true;
    }
    ops->ServerSendStatus(context->trailing_metadata_, status);
  }

  void RunHandler(const HandlerParameter& param) final {
    CallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus> ops;
    FillOps(param.server_context, &ops);
    param.call->PerformOps(&ops);
    param.call->cq()->Pluck(&ops);
  }
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_METHOD_HANDLER_IMPL_H
