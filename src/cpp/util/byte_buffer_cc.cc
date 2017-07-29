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

#include <grpc++/support/byte_buffer.h>
#include <grpc++/generic/async_generic_service.h>
#include <grpc/byte_buffer_reader.h>

#include <atomic>

namespace grpc {

ByteBuffer::ByteBuffer(const Slice* slices, size_t nslices) {
  // The following assertions check that the representation of a grpc::Slice is
  // identical to that of a grpc_slice:  it has a grpc_slice field, and nothing
  // else.
  static_assert(std::is_same<decltype(slices[0].slice_), grpc_slice>::value,
                "Slice must have same representation as grpc_slice");
  static_assert(sizeof(Slice) == sizeof(grpc_slice),
                "Slice must have same representation as grpc_slice");
  // The const_cast is legal if grpc_raw_byte_buffer_create() does no more
  // than its advertised side effect of increasing the reference count of the
  // slices it processes, and such an increase does not affect the semantics
  // seen by the caller of this constructor.
  buffer_ = grpc_raw_byte_buffer_create(
      reinterpret_cast<grpc_slice*>(const_cast<Slice*>(slices)), nslices);
}

ByteBuffer::~ByteBuffer() {
  if (buffer_) {
    grpc_byte_buffer_destroy(buffer_);
  }
}

void ByteBuffer::Clear() {
  if (buffer_) {
    grpc_byte_buffer_destroy(buffer_);
    buffer_ = nullptr;
  }
}

Status ByteBuffer::Dump(std::vector<Slice>* slices) const {
  slices->clear();
  if (!buffer_) {
    return Status(StatusCode::FAILED_PRECONDITION, "Buffer not initialized");
  }
  grpc_byte_buffer_reader reader;
  if (!grpc_byte_buffer_reader_init(&reader, buffer_)) {
    return Status(StatusCode::INTERNAL,
                  "Couldn't initialize byte buffer reader");
  }
  grpc_slice s;
  while (grpc_byte_buffer_reader_next(&reader, &s)) {
    slices->push_back(Slice(s, Slice::STEAL_REF));
  }
  grpc_byte_buffer_reader_destroy(&reader);
  return Status::OK;
}

size_t ByteBuffer::Length() const {
  if (buffer_) {
    return grpc_byte_buffer_length(buffer_);
  } else {
    return 0;
  }
}

ByteBuffer::ByteBuffer(const ByteBuffer& buf)
    : buffer_(grpc_byte_buffer_copy(buf.buffer_)) {}

ByteBuffer& ByteBuffer::operator=(const ByteBuffer& buf) {
  Clear();  // first remove existing data
  if (buf.buffer_) {
    buffer_ = grpc_byte_buffer_copy(buf.buffer_);  // then copy
  }
  return *this;
}

void ByteBuffer::Swap(ByteBuffer* other) {
  grpc_byte_buffer* tmp = other->buffer_;
  other->buffer_ = buffer_;
  buffer_ = tmp;
}

void UnknownMethodHandler::FillOps(ServerContext* context, CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage, CallOpServerSendStatus>* ops) {
  Status status(StatusCode::UNIMPLEMENTED, "");
  static std::atomic<int> count(0);
  if (!context->sent_initial_metadata_) {
    ops->SendInitialMetadata(context->initial_metadata_,
                             context->initial_metadata_flags());
    if (context->compression_level_set()) {
      ops->set_compression_level(context->compression_level());
    }
    context->sent_initial_metadata_ = true;
    char b[1024];
    snprintf(b, sizeof(b),
"<html><head><link rel=icon href=\"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAA"
"AABJRU5ErkJggg==\"></head>"
"<body>This <b>is</b> HTML: %d. Method: %s</body></html>",
             count++, static_cast<GenericServerContext*>(context)->method().c_str());
    Slice s(SliceFromCopiedString(b), Slice::STEAL_REF);
    ops->SendMessage(ByteBuffer(&s, 1), WriteOptions().set_raw());
  }
  ops->ServerSendStatus(context->trailing_metadata_, Status::OK);
}

}  // namespace grpc
