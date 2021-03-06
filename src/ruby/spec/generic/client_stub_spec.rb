# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

require 'grpc'

Thread.abort_on_exception = true

def wakey_thread(&blk)
  n = GRPC::Notifier.new
  t = Thread.new do
    blk.call(n)
  end
  t.abort_on_exception = true
  n.wait
  t
end

def load_test_certs
  test_root = File.join(File.dirname(File.dirname(__FILE__)), 'testdata')
  files = ['ca.pem', 'server1.key', 'server1.pem']
  files.map { |f| File.open(File.join(test_root, f)).read }
end

include GRPC::Core::StatusCodes
include GRPC::Core::TimeConsts
include GRPC::Core::CallOps

# check that methods on a finished/closed call t crash
def check_op_view_of_finished_client_call(op_view,
                                          expected_metadata,
                                          expected_trailing_metadata)
  # use read_response_stream to try to iterate through
  # possible response stream
  fail('need something to attempt reads') unless block_given?
  expect do
    resp = op_view.execute
    yield resp
  end.to raise_error(GRPC::Core::CallError)

  expect { op_view.start_call }.to raise_error(RuntimeError)

  sanity_check_values_of_accessors(op_view,
                                   expected_metadata,
                                   expected_trailing_metadata)

  expect do
    op_view.wait
    op_view.cancel
    op_view.write_flag = 1
  end.to_not raise_error
end

def sanity_check_values_of_accessors(op_view,
                                     expected_metadata,
                                     expected_trailing_metadata)
  expected_status = Struct::Status.new
  expected_status.code = 0
  expected_status.details = 'OK'
  expected_status.metadata = expected_trailing_metadata

  expect(op_view.status).to eq(expected_status)
  expect(op_view.metadata).to eq(expected_metadata)
  expect(op_view.trailing_metadata).to eq(expected_trailing_metadata)

  expect(op_view.cancelled?).to be(false)
  expect(op_view.write_flag).to be(nil)

  # The deadline attribute of a call can be either
  # a GRPC::Core::TimeSpec or a Time, which are mutually exclusive.
  # TODO: fix so that the accessor always returns the same type.
  expect(op_view.deadline.is_a?(GRPC::Core::TimeSpec) ||
         op_view.deadline.is_a?(Time)).to be(true)
end

describe 'ClientStub' do
  let(:noop) { proc { |x| x } }

  before(:each) do
    Thread.abort_on_exception = true
    @server = nil
    @method = 'an_rpc_method'
    @pass = OK
    @fail = INTERNAL
    @metadata = { k1: 'v1', k2: 'v2' }
  end

  after(:each) do
    @server.close(from_relative_time(2)) unless @server.nil?
  end

  describe '#new' do
    let(:fake_host) { 'localhost:0' }
    it 'can be created from a host and args' do
      opts = { channel_args: { a_channel_arg: 'an_arg' } }
      blk = proc do
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'can be created with an channel override' do
      opts = {
        channel_args: { a_channel_arg: 'an_arg' },
        channel_override: @ch
      }
      blk = proc do
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).not_to raise_error
    end

    it 'cannot be created with a bad channel override' do
      blk = proc do
        opts = {
          channel_args: { a_channel_arg: 'an_arg' },
          channel_override: Object.new
        }
        GRPC::ClientStub.new(fake_host, :this_channel_is_insecure, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'cannot be created with bad credentials' do
      blk = proc do
        opts = { channel_args: { a_channel_arg: 'an_arg' } }
        GRPC::ClientStub.new(fake_host, Object.new, **opts)
      end
      expect(&blk).to raise_error
    end

    it 'can be created with test test credentials' do
      certs = load_test_certs
      blk = proc do
        opts = {
          channel_args: {
            GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr',
            a_channel_arg: 'an_arg'
          }
        }
        creds = GRPC::Core::ChannelCredentials.new(certs[0], nil, nil)
        GRPC::ClientStub.new(fake_host, creds,  **opts)
      end
      expect(&blk).to_not raise_error
    end
  end

  describe '#request_response', request_response: true do
    before(:each) do
      @sent_msg, @resp = 'a_msg', 'a_reply'
    end

    shared_examples 'request response' do
      it 'should send a request to/receive a reply from a server' do
        server_port = create_test_server
        th = run_request_response(@sent_msg, @resp, @pass)
        stub = GRPC::ClientStub.new("localhost:#{server_port}",
                                    :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass,
                                  expected_metadata: { k1: 'v1', k2: 'v2' })
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should send a request when configured using an override channel' do
        server_port = create_test_server
        alt_host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @pass)
        ch = GRPC::Core::Channel.new(alt_host, nil, :this_channel_is_insecure)
        stub = GRPC::ClientStub.new('ignored-host',
                                    :this_channel_is_insecure,
                                    channel_override: ch)
        expect(get_response(stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not OK' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_request_response(@sent_msg, @resp, @fail)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        blk = proc { get_response(stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should receive UNAUTHENTICATED if call credentials plugin fails' do
        server_port = create_secure_test_server
        th = run_request_response(@sent_msg, @resp, @pass)

        certs = load_test_certs
        secure_channel_creds = GRPC::Core::ChannelCredentials.new(
          certs[0], nil, nil)
        secure_stub_opts = {
          channel_args: {
            GRPC::Core::Channel::SSL_TARGET => 'foo.test.google.fr'
          }
        }
        stub = GRPC::ClientStub.new("localhost:#{server_port}",
                                    secure_channel_creds, **secure_stub_opts)

        error_message = 'Failing call credentials callback'
        failing_auth = proc do
          fail error_message
        end
        creds = GRPC::Core::CallCredentials.new(failing_auth)

        unauth_error_occured = false
        begin
          get_response(stub, credentials: creds)
        rescue GRPC::Unauthenticated => e
          unauth_error_occured = true
          expect(e.details.include?(error_message)).to be true
        end
        expect(unauth_error_occured).to eq(true)

        # Kill the server thread so tests can complete
        th.kill
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        server_port = create_test_server
        host = "localhost:#{server_port}"
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect do
          get_response(stub)
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end
    end

    describe 'without a call operation' do
      def get_response(stub, credentials: nil)
        puts credentials.inspect
        stub.request_response(@method, @sent_msg, noop, noop,
                              metadata: @metadata,
                              credentials: credentials)
      end

      it_behaves_like 'request response'
    end

    describe 'via a call operation' do
      after(:each) do
        # make sure op.wait doesn't hang, even if there's a bad status
        @op.wait
      end
      def get_response(stub, run_start_call_first: false, credentials: nil)
        @op = stub.request_response(@method, @sent_msg, noop, noop,
                                    return_op: true,
                                    metadata: @metadata,
                                    deadline: from_relative_time(2),
                                    credentials: credentials)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        result = @op.execute
        result
      end

      it_behaves_like 'request response'

      def run_op_view_metadata_test(run_start_call_first)
        server_port = create_test_server
        host = "localhost:#{server_port}"

        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_request_response(
          @sent_msg, @resp, @pass,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(
          get_response(stub,
                       run_start_call_first: run_start_call_first)).to eq(@resp)
        th.join
      end

      it 'sends metadata to the server ok when running start_call first' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) { |r| p r }
      end

      it 'does not crash when used after the call has been finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) { |r| p r }
      end
    end
  end

  describe '#client_streamer', client_streamer: true do
    before(:each) do
      Thread.abort_on_exception = true
      server_port = create_test_server
      host = "localhost:#{server_port}"
      @stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
      @resp = 'a_reply'
    end

    shared_examples 'client streaming' do
      it 'should send requests to/receive a reply from a server' do
        th = run_client_streamer(@sent_msgs, @resp, @pass)
        expect(get_response(@stub)).to eq(@resp)
        th.join
      end

      it 'should send metadata to the server ok' do
        th = run_client_streamer(@sent_msgs, @resp, @pass,
                                 expected_metadata: @metadata)
        expect(get_response(@stub)).to eq(@resp)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        th = run_client_streamer(@sent_msgs, @resp, @fail)
        blk = proc { get_response(@stub) }
        expect(&blk).to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        expect do
          get_response(@stub)
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end
    end

    describe 'without a call operation' do
      def get_response(stub)
        stub.client_streamer(@method, @sent_msgs, noop, noop,
                             metadata: @metadata)
      end

      it_behaves_like 'client streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        # make sure op.wait doesn't hang, even if there's a bad status
        @op.wait
      end
      def get_response(stub, run_start_call_first: false)
        @op = stub.client_streamer(@method, @sent_msgs, noop, noop,
                                   return_op: true, metadata: @metadata)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        result = @op.execute
        result
      end

      it_behaves_like 'client streaming'

      def run_op_view_metadata_test(run_start_call_first)
        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_client_streamer(
          @sent_msgs, @resp, @pass,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        expect(
          get_response(@stub,
                       run_start_call_first: run_start_call_first)).to eq(@resp)
        th.join
      end

      it 'sends metadata to the server ok when running start_call first' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) { |r| p r }
      end

      it 'does not crash when used after the call has been finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) { |r| p r }
      end
    end
  end

  describe '#server_streamer', server_streamer: true do
    before(:each) do
      @sent_msg = 'a_msg'
      @replys = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
    end

    shared_examples 'server streaming' do
      it 'should send a request to/receive replies from a server' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @pass)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect(get_responses(stub).collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should send metadata to the server ok' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @fail,
                                 expected_metadata: { k1: 'v1', k2: 'v2' })
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      it 'should raise ArgumentError if metadata contains invalid values' do
        @metadata.merge!(k3: 3)
        server_port = create_test_server
        host = "localhost:#{server_port}"
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        expect do
          get_responses(stub)
        end.to raise_error(ArgumentError,
                           /Header values must be of type string or array/)
      end

      it 'the call terminates when there is an unmarshalling error' do
        server_port = create_test_server
        host = "localhost:#{server_port}"
        th = run_server_streamer(@sent_msg, @replys, @pass)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)

        unmarshal = proc { fail(ArgumentError, 'test unmarshalling error') }
        expect do
          get_responses(stub, unmarshal: unmarshal).collect { |r| r }
        end.to raise_error(ArgumentError, 'test unmarshalling error')
        th.join
      end
    end

    describe 'without a call operation' do
      def get_responses(stub, unmarshal: noop)
        e = stub.server_streamer(@method, @sent_msg, noop, unmarshal,
                                 metadata: @metadata)
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        @op.wait # make sure wait doesn't hang
      end
      def get_responses(stub, run_start_call_first: false, unmarshal: noop)
        @op = stub.server_streamer(@method, @sent_msg, noop, unmarshal,
                                   return_op: true,
                                   metadata: @metadata)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        e = @op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'server streaming'

      def run_op_view_metadata_test(run_start_call_first)
        server_port = create_test_server
        host = "localhost:#{server_port}"
        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_server_streamer(
          @sent_msg, @replys, @pass,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        stub = GRPC::ClientStub.new(host, :this_channel_is_insecure)
        e = get_responses(stub, run_start_call_first: run_start_call_first)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'should send metadata to the server ok when start_call is run first' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| p r }
        end
      end

      it 'does not crash when used after the call has been finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| p r }
        end
      end
    end
  end

  describe '#bidi_streamer', bidi: true do
    before(:each) do
      @sent_msgs = Array.new(3) { |i| 'msg_' + (i + 1).to_s }
      @replys = Array.new(3) { |i| 'reply_' + (i + 1).to_s }
      server_port = create_test_server
      @host = "localhost:#{server_port}"
    end

    shared_examples 'bidi streaming' do
      it 'supports sending all the requests first' do
        th = run_bidi_streamer_handle_inputs_first(@sent_msgs, @replys,
                                                   @pass)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@replys)
        th.join
      end

      it 'supports client-initiated ping pong' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, true)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'supports a server-initiated ping pong' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, false)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'should raise an error if the status is not ok' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @fail, false)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect { e.collect { |r| r } }.to raise_error(GRPC::BadStatus)
        th.join
      end

      # TODO: add test for metadata-related ArgumentError in a bidi call once
      # issue mentioned in https://github.com/grpc/grpc/issues/10526 is fixed

      it 'should send metadata to the server ok' do
        th = run_bidi_streamer_echo_ping_pong(@sent_msgs, @pass, true,
                                              expected_metadata: @metadata)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end
    end

    describe 'without a call operation' do
      def get_responses(stub)
        e = stub.bidi_streamer(@method, @sent_msgs, noop, noop,
                               metadata: @metadata)
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'
    end

    describe 'via a call operation' do
      after(:each) do
        @op.wait # make sure wait doesn't hang
      end
      def get_responses(stub, run_start_call_first: false)
        @op = stub.bidi_streamer(@method, @sent_msgs, noop, noop,
                                 return_op: true,
                                 metadata: @metadata)
        expect(@op).to be_a(GRPC::ActiveCall::Operation)
        @op.start_call if run_start_call_first
        e = @op.execute
        expect(e).to be_a(Enumerator)
        e
      end

      it_behaves_like 'bidi streaming'

      def run_op_view_metadata_test(run_start_call_first)
        @server_initial_md = { 'sk1' => 'sv1', 'sk2' => 'sv2' }
        @server_trailing_md = { 'tk1' => 'tv1', 'tk2' => 'tv2' }
        th = run_bidi_streamer_echo_ping_pong(
          @sent_msgs, @pass, true,
          expected_metadata: @metadata,
          server_initial_md: @server_initial_md,
          server_trailing_md: @server_trailing_md)
        stub = GRPC::ClientStub.new(@host, :this_channel_is_insecure)
        e = get_responses(stub, run_start_call_first: run_start_call_first)
        expect(e.collect { |r| r }).to eq(@sent_msgs)
        th.join
      end

      it 'can run start_call before executing the call' do
        run_op_view_metadata_test(true)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| p r }
        end
      end

      it 'doesnt crash when op_view used after call has finished' do
        run_op_view_metadata_test(false)
        check_op_view_of_finished_client_call(
          @op, @server_initial_md, @server_trailing_md) do |responses|
          responses.each { |r| p r }
        end
      end
    end
  end

  def run_server_streamer(expected_input, replys, status,
                          expected_metadata: {},
                          server_initial_md: {},
                          server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      expect(c.remote_read).to eq(expected_input)
      replys.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
    end
  end

  def run_bidi_streamer_handle_inputs_first(expected_inputs, replys,
                                            status)
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(notifier)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      replys.each { |r| c.remote_send(r) }
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true)
    end
  end

  def run_bidi_streamer_echo_ping_pong(expected_inputs, status, client_starts,
                                       expected_metadata: {},
                                       server_initial_md: {},
                                       server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      expected_inputs.each do |i|
        if client_starts
          expect(c.remote_read).to eq(i)
          c.remote_send(i)
        else
          c.remote_send(i)
          expect(c.remote_read).to eq(i)
        end
      end
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
    end
  end

  def run_client_streamer(expected_inputs, resp, status,
                          expected_metadata: {},
                          server_initial_md: {},
                          server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      expected_inputs.each { |i| expect(c.remote_read).to eq(i) }
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
    end
  end

  def run_request_response(expected_input, resp, status,
                           expected_metadata: {},
                           server_initial_md: {},
                           server_trailing_md: {})
    wanted_metadata = expected_metadata.clone
    wakey_thread do |notifier|
      c = expect_server_to_be_invoked(
        notifier, metadata_to_send: server_initial_md)
      expect(c.remote_read).to eq(expected_input)
      wanted_metadata.each do |k, v|
        expect(c.metadata[k.to_s]).to eq(v)
      end
      c.remote_send(resp)
      c.send_status(status, status == @pass ? 'OK' : 'NOK', true,
                    metadata: server_trailing_md)
    end
  end

  def create_secure_test_server
    certs = load_test_certs
    secure_credentials = GRPC::Core::ServerCredentials.new(
      nil, [{ private_key: certs[1], cert_chain: certs[2] }], false)

    @server = GRPC::Core::Server.new(nil)
    @server.add_http2_port('0.0.0.0:0', secure_credentials)
  end

  def create_test_server
    @server = GRPC::Core::Server.new(nil)
    @server.add_http2_port('0.0.0.0:0', :this_port_is_insecure)
  end

  def expect_server_to_be_invoked(notifier, metadata_to_send: nil)
    @server.start
    notifier.notify(nil)
    recvd_rpc = @server.request_call
    recvd_call = recvd_rpc.call
    recvd_call.metadata = recvd_rpc.metadata
    recvd_call.run_batch(SEND_INITIAL_METADATA => metadata_to_send)
    GRPC::ActiveCall.new(recvd_call, noop, noop, INFINITE_FUTURE,
                         metadata_received: true)
  end
end
