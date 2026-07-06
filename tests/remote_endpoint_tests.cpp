#include "LibLsp/JsonRpc/MessageJsonHandler.h"
#include "LibLsp/JsonRpc/Endpoint.h"
#include "LibLsp/JsonRpc/Condition.h"
#include "LibLsp/JsonRpc/RemoteEndPoint.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/general/exit.h"
#include "LibLsp/lsp/general/initialize.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

struct RemoteEndPointTestAccess
{
    static bool Dispatch(RemoteEndPoint& endpoint, std::string const& content, uint64_t sequence)
    {
        return endpoint.dispatch(content, sequence);
    }
};

namespace
{
using test::DummyLog;
using test::Expect;
using test::FeedableIStream;
using test::StringIStream;
using test::StringOStream;

static_assert(
    lsp::detail::is_valid_async_request_return<lsp::future<td_initialize::response>, td_initialize::response>::value,
    "request handlers must accept future<Response>");
static_assert(
    lsp::detail::is_valid_async_request_return<
        lsp::future<lsp::ResponseOrError<td_initialize::response>>,
        td_initialize::response>::value,
    "request handlers must accept future<ResponseOrError<Response>>");
static_assert(
    !lsp::detail::is_valid_async_request_return<lsp::future<int>, td_initialize::response>::value,
    "request handlers must reject futures with unrelated value types");

struct MoveOnlyFutureValue
{
    MoveOnlyFutureValue() = default;
    explicit MoveOnlyFutureValue(int value) : value(value)
    {
    }
    MoveOnlyFutureValue(MoveOnlyFutureValue const&) = delete;
    MoveOnlyFutureValue& operator=(MoveOnlyFutureValue const&) = delete;
    MoveOnlyFutureValue(MoveOnlyFutureValue&& other) noexcept : value(other.value)
    {
        other.value = 0;
    }
    MoveOnlyFutureValue& operator=(MoveOnlyFutureValue&& other) noexcept
    {
        value = other.value;
        other.value = 0;
        return *this;
    }

    int value = 0;
};

std::string WaitForOutputContaining(std::shared_ptr<StringOStream> const& output_stream, std::string const& needle)
{
    std::string output;
    for (int i = 0; i < 100; ++i)
    {
        output = output_stream->snapshot();
        if (output.find(needle) != std::string::npos)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return output;
}

std::string WaitForOutputContainingAll(std::shared_ptr<StringOStream> const& output_stream, std::vector<std::string> const& needles)
{
    std::string output;
    for (int i = 0; i < 100; ++i)
    {
        output = output_stream->snapshot();
        bool found_all = true;
        for (auto const& needle : needles)
        {
            if (output.find(needle) == std::string::npos)
            {
                found_all = false;
                break;
            }
        }
        if (found_all)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return output;
}

std::string MakeDelimitedInput(std::string const& body)
{
    return body + "\n// -----\n";
}

std::vector<std::string> ExtractFrameBodies(std::string const& output)
{
    std::vector<std::string> bodies;
    size_t pos = 0;
    while (pos < output.size())
    {
        size_t const header_end = output.find("\r\n\r\n", pos);
        if (header_end == std::string::npos)
        {
            break;
        }
        size_t const length_start = output.find("Content-Length:", pos);
        if (length_start == std::string::npos || length_start > header_end)
        {
            break;
        }
        size_t const value_start = length_start + std::string("Content-Length:").size();
        int const length = std::atoi(output.substr(value_start, header_end - value_start).c_str());
        size_t const body_start = header_end + 4;
        if (length < 0 || body_start + static_cast<size_t>(length) > output.size())
        {
            break;
        }
        bodies.push_back(output.substr(body_start, static_cast<size_t>(length)));
        pos = body_start + static_cast<size_t>(length);
    }
    return bodies;
}

bool WaitUntil(std::function<bool()> const& predicate, int attempts = 100, int delay_ms = 10)
{
    for (int i = 0; i < attempts; ++i)
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    return predicate();
}

std::string ProcessSingleInput(std::string const& input, std::string const& wait_for = "")
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    json_handler->SetRequestJsonHandler(
        td_initialize::request::kMethodInfo,
        [](Reader& visitor)
        {
            return td_initialize::request::ReflectReader(visitor);
        });
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<StringIStream>(input);
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    std::string output;
    if (!wait_for.empty())
    {
        output = WaitForOutputContaining(output_stream, wait_for);
    }
    else
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        output = output_stream->snapshot();
    }
    point.stop();
    return output;
}

void TestDuplicateRequestIdReturnsError()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    StringOStream output;
    StringIStream input("");
    auto input_stream = std::make_shared<StringIStream>("");
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request first;
    first.id.set(7);
    auto first_future = point.send(first);

    td_initialize::request duplicate;
    duplicate.id.set(7);
    auto duplicate_future = point.send(duplicate);
    auto duplicate_result = duplicate_future.get();

    Expect(duplicate_result.is_error, "duplicate request id must return an error response");
    Expect(
        duplicate_result.error.error.message == "Failed to send request.",
        "duplicate request id must report failed send");

    point.stop();
}

void TestWaitResponseTimeoutClearsPendingRequest()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<StringIStream>("");
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request timed_out;
    timed_out.id.set(99);
    auto const response = point.waitResponse(timed_out, 200);
    Expect(response == nullptr, "waitResponse must return null on timeout");

    td_initialize::request retry;
    retry.id.set(99);
    auto retry_future = point.send(retry);
    auto const status = retry_future.wait_for(std::chrono::milliseconds(50));
    Expect(
        status == lsp::future_status::timeout,
        "request id must be reusable after timeout cleanup");

    point.stop();
}

void TestBadOutputStreamReturnsError()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto output_stream = std::make_shared<StringOStream>();
    output_stream->set_bad(true);

    RemoteEndPoint point(json_handler, endpoint, log);

    td_initialize::request req;
    req.id.set(3);
    auto future = point.send(req);
    auto result = future.get();

    Expect(result.is_error, "bad output stream must return an error response");
    Expect(
        result.error.error.message == "Failed to send request.",
        "bad output stream must report failed send");
}

void TestStopCompletesPendingRequestWithError()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<StringIStream>("");
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request req;
    req.id.set(123);
    auto future = point.send(req);

    point.stop();

    auto const status = future.wait_for(std::chrono::milliseconds(50));
    Expect(status == lsp::future_status::ready, "stop must complete pending request futures");
    if (status != lsp::future_status::ready)
    {
        return;
    }

    auto result = future.get();
    Expect(result.is_error, "stopped pending request must return an error response");
    Expect(
        result.error.error.message == "Remote endpoint stopped.",
        "stopped pending request must report endpoint stop");
}

void TestStopCancelsRunningRequestHandlers()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<bool> handler_entered {false};
    std::atomic<bool> handler_saw_cancel {false};
    std::atomic<bool> handler_exited {false};

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::ResponseOrError<td_initialize::response>
        {
            handler_entered.store(true, std::memory_order_relaxed);
            for (int i = 0; i < 200; ++i)
            {
                if (monitor && monitor())
                {
                    handler_saw_cancel.store(true, std::memory_order_relaxed);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            handler_exited.store(true, std::memory_order_relaxed);

            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1701,"method":"initialize","params":{}})"));
    Expect(
        WaitUntil([&]() { return handler_entered.load(std::memory_order_relaxed); }),
        "request handler must start before stop is called");

    point.stop();

    Expect(
        WaitUntil([&]() { return handler_exited.load(std::memory_order_relaxed); }),
        "stop must let a running handler observe cancellation and exit");
    Expect(
        handler_saw_cancel.load(std::memory_order_relaxed),
        "stop must cancel running request handlers through their CancelMonitor");
}

void TestStopFromNotificationHandlerDoesNotDeadlock()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<bool> handler_entered {false};
    std::atomic<bool> stop_returned {false};

    auto point = std::make_unique<RemoteEndPoint>(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    RemoteEndPoint* point_ptr = point.get();
    point->registerHandler(
        [&](Notify_Exit::notify const&)
        {
            handler_entered.store(true, std::memory_order_relaxed);
            point_ptr->stop();
            stop_returned.store(true, std::memory_order_relaxed);
        });
    point->startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"exit","params":{}})"));

    bool const stopped = WaitUntil([&]() { return stop_returned.load(std::memory_order_relaxed); }, 200, 10);
    Expect(handler_entered.load(std::memory_order_relaxed), "exit notification handler must run");
    Expect(stopped, "stop called from a notification handler must not deadlock");
    if (stopped)
    {
        point.reset();
    }
    else
    {
        // Avoid calling the destructor if the failure mode is a stuck self-stop.
        point.release();
    }
}

void TestAsyncFutureCompletedAfterStopIsDroppedSafely()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::mutex promise_mutex;
    std::shared_ptr<lsp::promise<td_initialize::response>> pending_promise;
    std::atomic<bool> handler_started {false};

    auto point = new RemoteEndPoint(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point->registerHandler(
        [&](td_initialize::request const&) -> lsp::future<td_initialize::response>
        {
            auto promise = std::make_shared<lsp::promise<td_initialize::response>>();
            auto future = promise->get_future();
            {
                std::lock_guard<std::mutex> lock(promise_mutex);
                pending_promise = promise;
            }
            handler_started.store(true, std::memory_order_relaxed);
            return future;
        });
    point->startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1703,"method":"initialize","params":{}})"));
    Expect(
        WaitUntil([&]() { return handler_started.load(std::memory_order_relaxed); }),
        "async request handler must start before stop");

    std::shared_ptr<lsp::promise<td_initialize::response>> promise;
    {
        std::lock_guard<std::mutex> lock(promise_mutex);
        promise = pending_promise;
    }
    Expect(promise != nullptr, "async request handler must expose its pending promise");

    point->stop();
    std::string const output_before_completion = output_stream->snapshot();
    delete point;

    if (promise)
    {
        td_initialize::response rsp;
        rsp.id.set(1703);
        promise->set_value(std::move(rsp));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Expect(
        output_stream->snapshot() == output_before_completion,
        "async future completed after stop/destruction must not write a late response");
}

void TestUnregisteredRequestReturnsMethodNotFound()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    json_handler->SetRequestJsonHandler(
        td_initialize::request::kMethodInfo,
        [](Reader& visitor)
        {
            return td_initialize::request::ReflectReader(visitor);
        });
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":77,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"code\":-32601");
    point.stop();

    Expect(
        output.find("\"id\":77") != std::string::npos,
        "MethodNotFound response must keep the original request id");
    Expect(
        output.find("\"code\":-32601") != std::string::npos,
        "unregistered request must return MethodNotFound");
    Expect(
        output.find("Method not found: initialize") != std::string::npos,
        "MethodNotFound response must include the method name");
}

void TestUnregisteredRequestPreservesStringIdInMethodNotFound()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    json_handler->SetRequestJsonHandler(
        td_initialize::request::kMethodInfo,
        [](Reader& visitor)
        {
            return td_initialize::request::ReflectReader(visitor);
        });
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":"abc-77","method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"code\":-32601");
    point.stop();

    Expect(
        output.find(R"("id":"abc-77")") != std::string::npos,
        "MethodNotFound response must preserve string request ids");
    Expect(
        output.find("\"code\":-32601") != std::string::npos,
        "string-id unregistered request must return MethodNotFound");
}

void TestRegisteredRequestDoesNotReturnMethodNotFound()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":88,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":88");
    point.stop();

    Expect(output.find("\"id\":88") != std::string::npos, "registered request must produce a response");
    Expect(
        output.find("\"code\":-32601") == std::string::npos,
        "registered request must not produce MethodNotFound");
}

void TestMalformedJsonDoesNotCrash()
{
    auto const output = ProcessSingleInput("Content-Length: 5\r\n\r\n{bad}");

    Expect(output.empty(), "malformed JSON must not produce a response");
}

void TestWrongJsonRpcVersionIsRejected()
{
    std::string const request = R"({"jsonrpc":"1.0","id":1,"method":"initialize","params":{}})";
    auto const output = ProcessSingleInput(test::MakeLspFrame(request));

    Expect(output.empty(), "wrong jsonrpc version must not produce a response");
}

void TestNonStringJsonRpcVersionIsRejected()
{
    std::string const request = R"({"jsonrpc":2.0,"id":1,"method":"initialize","params":{}})";
    auto const output = ProcessSingleInput(test::MakeLspFrame(request));

    Expect(output.empty(), "non-string jsonrpc version must be rejected without producing a response");
}

void TestNonStringMethodIsRejected()
{
    std::string const request = R"({"jsonrpc":"2.0","id":1,"method":123,"params":{}})";
    auto const request_output = ProcessSingleInput(test::MakeLspFrame(request));
    std::string const notification = R"({"jsonrpc":"2.0","method":123,"params":{}})";
    auto const notification_output = ProcessSingleInput(test::MakeLspFrame(notification));

    Expect(request_output.empty(), "request with non-string method must be rejected without producing a response");
    Expect(
        notification_output.empty(),
        "notification with non-string method must be rejected without producing a response");
}

void TestRequestMissingParamsDoesNotCrashEndpoint()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<int> handled_count {0};
    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point.registerHandler(
        [&](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            handled_count.fetch_add(1, std::memory_order_relaxed);
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1901,"method":"initialize"})"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1902,"method":"initialize","params":{}})"));

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":1902");
    point.stop();

    Expect(output.find("\"id\":1902") != std::string::npos, "endpoint must still handle requests after missing params");
    Expect(
        handled_count.load(std::memory_order_relaxed) >= 1,
        "endpoint must stay alive after a request that omits params");
}

void TestResponseWithNoMatchingRequestIsIgnored()
{
    std::string const response = R"({"jsonrpc":"2.0","id":999,"result":{}})";
    auto const output = ProcessSingleInput(test::MakeLspFrame(response));

    Expect(output.empty(), "unknown response id must not produce output");
}

void TestMessageWithIdAndMethodUsesRequestPath()
{
    std::string const request = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    auto const output = ProcessSingleInput(test::MakeLspFrame(request), "\"code\":-32601");

    Expect(
        output.find("\"code\":-32601") != std::string::npos,
        "message with id and method must be treated as a request");
}

void TestMessageWithNoMethodNoResultIsRejected()
{
    std::string const message = R"({"jsonrpc":"2.0","id":1})";
    auto const output = ProcessSingleInput(test::MakeLspFrame(message));

    Expect(output.empty(), "message without method/result/error must be rejected");
}

void TestSendRequestAndReceiveSuccessResponse()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request req;
    auto future = point.send(req);
    WaitForOutputContaining(output_stream, "\"method\":\"initialize\"");

    std::string const response =
        std::string(R"({"jsonrpc":"2.0","id":)") + ToString(req.id) + R"(,"result":{"capabilities":{}}})";
    input_stream->append(test::MakeLspFrame(response));

    auto const status = future.wait_for(std::chrono::milliseconds(1000));
    Expect(status == lsp::future_status::ready, "success response must complete the request future");
    if (status == lsp::future_status::ready)
    {
        auto result = future.get();
        Expect(!result.is_error, "success response must produce a non-error result");
    }
    point.stop();
}

void TestSendRequestAndReceiveErrorResponse()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request req;
    auto future = point.send(req);
    WaitForOutputContaining(output_stream, "\"method\":\"initialize\"");

    std::string const response = std::string(R"({"jsonrpc":"2.0","id":)") + ToString(req.id) +
                                 R"(,"error":{"code":-32600,"message":"test error"}})";
    input_stream->append(test::MakeLspFrame(response));

    auto const status = future.wait_for(std::chrono::milliseconds(1000));
    Expect(status == lsp::future_status::ready, "error response must complete the request future");
    if (status == lsp::future_status::ready)
    {
        auto result = future.get();
        Expect(result.is_error, "error response must produce an error result");
        Expect(result.error.error.code == lsErrorCodes::InvalidRequest, "error code must round-trip");
        Expect(result.error.error.message == "test error", "error message must round-trip");
    }
    point.stop();
}

void TestDuplicateResponseCompletesFutureOnce()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    std::atomic<int> success_calls {0};
    std::atomic<int> error_calls {0};
    td_initialize::request req;
    req.id.set(1801);
    point.send(
        req,
        [&](td_initialize::response const& rsp)
        {
            Expect(rsp.id == req.id, "duplicate response test must receive the matching response id");
            success_calls.fetch_add(1, std::memory_order_relaxed);
        },
        [&](Rsp_Error const&)
        {
            error_calls.fetch_add(1, std::memory_order_relaxed);
        });
    WaitForOutputContaining(output_stream, "\"id\":1801");

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1801,"result":{"capabilities":{}}})"));
    Expect(
        WaitUntil([&]() { return success_calls.load(std::memory_order_relaxed) == 1; }),
        "first response must complete the request callback");
    input_stream->append(
        test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1801,"result":{"capabilities":{"hoverProvider":true}}})"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    point.stop();

    Expect(success_calls.load(std::memory_order_relaxed) == 1, "duplicate response must not complete a request twice");
    Expect(error_calls.load(std::memory_order_relaxed) == 0, "duplicate response must not report an error");
}

void TestSendRequestAndReceiveSuccessResponseWithStringId()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request req;
    req.id.set("client-alpha");
    auto future = point.send(req);
    WaitForOutputContaining(output_stream, R"("id":"client-alpha")");

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":"client-alpha","result":{"capabilities":{}}})"));

    auto const status = future.wait_for(std::chrono::milliseconds(1000));
    Expect(status == lsp::future_status::ready, "string-id success response must complete the request future");
    if (status == lsp::future_status::ready)
    {
        auto result = future.get();
        Expect(!result.is_error, "string-id success response must produce a non-error result");
        Expect(result.response.id == req.id, "string-id response must preserve the original request id");
    }
    point.stop();
}

void TestLateResponseAfterTimeoutIsIgnored()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request timed_out;
    timed_out.id.set(404);
    auto const timed_out_response = point.waitResponse(timed_out, 20);
    Expect(timed_out_response == nullptr, "waitResponse must time out before the late response arrives");

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":404,"result":{"capabilities":{}}})"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    td_initialize::request retry;
    retry.id.set(404);
    auto retry_future = point.send(retry);
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":404,"result":{"capabilities":{"hoverProvider":true}}})"));

    auto const status = retry_future.wait_for(std::chrono::milliseconds(1000));
    Expect(status == lsp::future_status::ready, "request id must remain reusable after a late timed-out response");
    if (status == lsp::future_status::ready)
    {
        auto result = retry_future.get();
        Expect(!result.is_error, "retry after late response must receive the new success response");
        Expect(
            result.response.result.capabilities.hoverProvider &&
                *result.response.result.capabilities.hoverProvider == true,
            "retry response must be the second response, not the ignored late response");
    }
    point.stop();
}

void TestCreateRequestAssignsMonotonicIds()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    RemoteEndPoint point(json_handler, endpoint, log);

    auto first = point.createRequest<td_initialize::request>();
    auto second = point.createRequest<td_initialize::request>();
    auto third = point.createRequest<td_initialize::request>();

    Expect(first.id.type == lsRequestId::kInt, "createRequest must assign an integer id");
    Expect(first.id.value < second.id.value, "second generated id must be greater than first");
    Expect(second.id.value < third.id.value, "third generated id must be greater than second");
}

void TestCancelRequestSendsCancelNotification()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request req;
    auto future = point.send(req);
    WaitForOutputContaining(output_stream, "\"method\":\"initialize\"");

    bool const cancelled = point.cancelRequest(req.id);
    std::string const output = WaitForOutputContaining(output_stream, "$/cancelRequest");

    Expect(cancelled, "cancelRequest must return true for a pending request");
    Expect(
        output.find(R"("method":"$/cancelRequest")") != std::string::npos,
        "cancelRequest must send a cancel notification");
    Expect(output.find("\"id\":" + ToString(req.id)) != std::string::npos, "cancel notification must include id");
    point.stop();
}

void TestPendingCancelArrivingBeforeRequestIsNotLost()
{
    // Covers the scheduling race where $/cancelRequest is dispatched before the
    // target request has registered its canceler.
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);

    std::string const cancel = R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":501}})";
    std::string const request = R"({"jsonrpc":"2.0","id":501,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(cancel) + test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    bool handler_saw_cancel = false;
    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::ResponseOrError<td_initialize::response>
        {
            handler_saw_cancel = monitor && monitor();
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":501");
    point.stop();

    Expect(handler_saw_cancel, "cancel notification that arrives before request registration must be applied");
    Expect(output.find("\"id\":501") != std::string::npos, "request with pending cancel must still receive a response");
}

void TestLateCancelAfterCompletedRequestDoesNotCancelReusedId()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<bool> saw_cancel;

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 2);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::ResponseOrError<td_initialize::response>
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                saw_cancel.push_back(monitor && monitor());
            }
            cv.notify_all();
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":710,"method":"initialize","params":{}})"));
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(
            lock,
            std::chrono::milliseconds(1000),
            [&]()
            {
                return saw_cancel.size() >= 1;
            });
    }

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":710}})"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":710,"method":"initialize","params":{}})"));
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool const handled_reused_id = cv.wait_for(
            lock,
            std::chrono::milliseconds(1000),
            [&]()
            {
                return saw_cancel.size() >= 2;
            });
        Expect(handled_reused_id, "reused request id must be handled after a late cancel");
    }
    point.stop();

    Expect(saw_cancel.size() == 2, "both original and reused request ids must be handled");
    if (saw_cancel.size() >= 2)
    {
        Expect(!saw_cancel[0], "original request must not be pre-cancelled");
        Expect(!saw_cancel[1], "late cancel for completed request must not cancel reused id");
    }
}

void TestOutOfOrderPendingCancelAppliesToReusedRequestId()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);

    std::vector<bool> saw_cancel;
    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::ResponseOrError<td_initialize::response>
        {
            saw_cancel.push_back(monitor && monitor());
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });

    std::string const first_request = R"({"jsonrpc":"2.0","id":1750,"method":"initialize","params":{}})";
    std::string const cancel_after_reused_request =
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":1750}})";
    std::string const reused_request = R"({"jsonrpc":"2.0","id":1750,"method":"initialize","params":{}})";

    Expect(
        RemoteEndPointTestAccess::Dispatch(point, first_request, 0),
        "first request must dispatch before testing id reuse");
    Expect(
        saw_cancel.size() == 1 && !saw_cancel[0],
        "first request must complete without a pending cancellation");

    // Deterministically model worker reordering: the cancel is later on the
    // wire than the reused request, but it reaches dispatch first.
    Expect(
        RemoteEndPointTestAccess::Dispatch(point, cancel_after_reused_request, 2),
        "out-of-order cancel notification must dispatch");
    Expect(
        RemoteEndPointTestAccess::Dispatch(point, reused_request, 1),
        "reused request must dispatch after the pending out-of-order cancel");

    Expect(saw_cancel.size() == 2, "both original and reused requests must be handled");
    if (saw_cancel.size() >= 2)
    {
        Expect(saw_cancel[1], "newer pending cancel must apply to the reused request id when sequence is higher");
    }
}

void TestStopClearsPendingCancelRequests()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto output_stream = std::make_shared<StringOStream>();

    std::mutex mutex;
    std::condition_variable cv;
    bool handler_called = false;
    bool handler_saw_cancel = false;

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::ResponseOrError<td_initialize::response>
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                handler_called = true;
                handler_saw_cancel = monitor && monitor();
            }
            cv.notify_all();
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });

    auto first_input = std::make_shared<StringIStream>(
        test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":720}})"));
    point.startProcessingMessages(first_input, output_stream);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    point.stop();

    auto second_input = std::make_shared<StringIStream>(
        test::MakeLspFrame(R"({"jsonrpc":"2.0","id":720,"method":"initialize","params":{}})"));
    point.startProcessingMessages(second_input, output_stream);
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool const handled = cv.wait_for(
            lock,
            std::chrono::milliseconds(1000),
            [&]()
            {
                return handler_called;
            });
        Expect(handled, "request after restart must be handled");
    }
    point.stop();

    Expect(!handler_saw_cancel, "pending cancel from a previous run must be cleared on stop");
}

void TestRunningRequestObservesCancellation()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::mutex mutex;
    std::condition_variable cv;
    bool handler_entered = false;
    std::atomic<bool> handler_saw_cancel {false};

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 2);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::ResponseOrError<td_initialize::response>
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                handler_entered = true;
            }
            cv.notify_all();

            for (int i = 0; i < 200; ++i)
            {
                if (monitor && monitor())
                {
                    handler_saw_cancel.store(true, std::memory_order_relaxed);
                    Rsp_Error error;
                    error.id = req.id;
                    error.error.code = lsErrorCodes::RequestCancelled;
                    error.error.message = "cancelled";
                    return error;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":620,"method":"initialize","params":{}})"));
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool const entered = cv.wait_for(
            lock,
            std::chrono::milliseconds(1000),
            [&]()
            {
                return handler_entered;
            });
        Expect(entered, "request handler must start before cancellation is sent");
    }

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":620}})"));
    std::string const output = WaitForOutputContaining(output_stream, "\"code\":-32800");
    point.stop();

    Expect(handler_saw_cancel.load(std::memory_order_relaxed), "running request handler must observe cancellation");
    Expect(output.find("\"code\":-32800") != std::string::npos, "cancelled request must report RequestCancelled");
}

void TestBurstRequestsAreAllDispatchedAndResponded()
{
    // Feeds a burst of framed requests at once to guard the producer-to-worker
    // scheduling path against dropped dispatches.
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);

    std::string input;
    std::vector<std::string> response_ids;
    for (int i = 0; i < 24; ++i)
    {
        int const id = 800 + i;
        input += test::MakeLspFrame(
            std::string(R"({"jsonrpc":"2.0","id":)") + std::to_string(id) + R"(,"method":"initialize","params":{}})");
        response_ids.push_back("\"id\":" + std::to_string(id));
    }

    auto input_stream = std::make_shared<StringIStream>(input);
    auto output_stream = std::make_shared<StringOStream>();
    std::mutex mutex;
    std::set<int> handled_ids;

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 4);
    point.registerHandler(
        [&](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                handled_ids.insert(req.id.value);
            }
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContainingAll(output_stream, response_ids);
    point.stop();

    Expect(handled_ids.size() == response_ids.size(), "RemoteEndPoint must dispatch every request in a burst");
    for (auto const& id : response_ids)
    {
        Expect(output.find(id) != std::string::npos, "RemoteEndPoint burst dispatch must produce every response");
    }
}

void TestMultipleWorkersProcessRequestsConcurrently()
{
    // Confirms max_workers is effective by blocking handlers until at least two
    // have entered concurrently.
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const first = R"({"jsonrpc":"2.0","id":201,"method":"initialize","params":{}})";
    std::string const second = R"({"jsonrpc":"2.0","id":202,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(first) + test::MakeLspFrame(second));
    auto output_stream = std::make_shared<StringOStream>();

    std::mutex mutex;
    std::condition_variable cv;
    int active_handlers = 0;
    int max_active_handlers = 0;
    int entered_handlers = 0;
    bool release_handlers = false;

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 4);
    point.registerHandler(
        [&](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            {
                std::unique_lock<std::mutex> lock(mutex);
                ++active_handlers;
                ++entered_handlers;
                if (max_active_handlers < active_handlers)
                {
                    max_active_handlers = active_handlers;
                }
                cv.notify_all();
                cv.wait(
                    lock,
                    [&]()
                    {
                        return release_handlers;
                    });
                --active_handlers;
            }

            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    {
        std::unique_lock<std::mutex> lock(mutex);
        bool const both_entered = cv.wait_for(
            lock,
            std::chrono::milliseconds(1000),
            [&]()
            {
                return entered_handlers >= 2;
            });
        Expect(both_entered, "both requests must enter handlers");
        Expect(max_active_handlers >= 2, "multiple workers must run request handlers concurrently");
        release_handlers = true;
    }
    cv.notify_all();

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":202");
    point.stop();

    Expect(output.find("\"id\":201") != std::string::npos, "first concurrent request must get a response");
    Expect(output.find("\"id\":202") != std::string::npos, "second concurrent request must get a response");
}

void TestConcurrentSendWritesCompleteFrames()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    std::vector<lsp::future<lsp::ResponseOrError<td_initialize::response>>> futures;
    std::mutex futures_mutex;
    std::vector<std::thread> senders;
    for (int i = 0; i < 16; ++i)
    {
        senders.emplace_back(
            [&, i]()
            {
                td_initialize::request req;
                req.id.set(3000 + i);
                auto future = point.send(req);
                std::lock_guard<std::mutex> lock(futures_mutex);
                futures.push_back(std::move(future));
            });
    }
    for (auto& sender : senders)
    {
        sender.join();
    }

    std::vector<std::string> needles;
    for (int i = 0; i < 16; ++i)
    {
        needles.push_back("\"id\":" + std::to_string(3000 + i));
    }
    std::string const output = WaitForOutputContainingAll(output_stream, needles);
    auto const bodies = ExtractFrameBodies(output);
    point.stop();

    Expect(bodies.size() == 16, "concurrent send must produce one complete frame per request");
    for (int i = 0; i < 16; ++i)
    {
        bool found = false;
        for (auto const& body : bodies)
        {
            if (body.find("\"id\":" + std::to_string(3000 + i)) != std::string::npos &&
                body.find(R"("method":"initialize")") != std::string::npos)
            {
                found = true;
                break;
            }
        }
        Expect(found, "concurrent send output must contain every request body exactly as a frame");
    }
}

void TestNotificationHandlerReceivesNotification()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();
    std::atomic<bool> notified {false};

    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [&](Notify_Exit::notify const&)
        {
            notified.store(true, std::memory_order_relaxed);
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"exit","params":{}})"));
    for (int i = 0; i < 100 && !notified.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    point.stop();

    Expect(notified.load(std::memory_order_relaxed), "registered notification handler must receive notifications");
}

void TestThrowingNotificationHandlerDoesNotStopEndpoint()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();
    std::atomic<bool> notification_seen {false};

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point.registerHandler(
        [&](Notify_Exit::notify const&)
        {
            notification_seen.store(true, std::memory_order_relaxed);
            throw std::runtime_error("notification handler failed");
        });
    point.registerHandler(
        [](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"exit","params":{}})"));
    Expect(
        WaitUntil([&]() { return notification_seen.load(std::memory_order_relaxed); }),
        "throwing notification handler must run");
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1950,"method":"initialize","params":{}})"));

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":1950");
    point.stop();

    Expect(output.find("\"id\":1950") != std::string::npos, "endpoint must handle requests after notification throw");
}

void TestConditionTimedWaitReturnsNotifiedValue()
{
    Condition<LspMessage> condition;
    std::thread notifier(
        [&]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::unique_ptr<LspMessage> msg(new Rsp_Error());
            condition.notify(std::move(msg));
        });

    auto msg = condition.wait(1000);
    notifier.join();

    Expect(msg != nullptr, "timed Condition wait must return the notified value");
}

void TestConditionTimedWaitStillTimesOut()
{
    Condition<LspMessage> condition;

    auto msg = condition.wait(10);

    Expect(msg == nullptr, "timed Condition wait must still return null on timeout");
}

void TestConditionWaitZeroBlocksUntilNotify()
{
    // RemoteEndPoint::internalWaitResponse uses wait(0) for indefinite blocking.
    Condition<LspMessage> condition;
    std::atomic<bool> wait_completed {false};
    std::unique_ptr<LspMessage> received;

    std::thread waiter(
        [&]()
        {
            received = condition.wait(0);
            wait_completed.store(true, std::memory_order_release);
        });

    for (int i = 0; i < 50 && !wait_completed.load(std::memory_order_acquire); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Expect(!wait_completed.load(std::memory_order_acquire), "Condition wait(0) must block until notify");

    std::unique_ptr<LspMessage> msg(new Rsp_Error());
    condition.notify(std::move(msg));
    waiter.join();

    Expect(wait_completed.load(std::memory_order_acquire), "Condition wait(0) must return after notify");
    Expect(received != nullptr, "Condition wait(0) must deliver the notified value");
}

void TestInternalWaitResponseSuccessTimeoutAndSendFailure()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request success_req;
    success_req.id.set(901);
    std::unique_ptr<LspMessage> success_response;
    std::thread success_waiter(
        [&]()
        {
            success_response = point.internalWaitResponse(success_req, 5000);
        });
    WaitForOutputContaining(output_stream, "\"id\":901");
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":901,"result":{"capabilities":{}}})"));
    success_waiter.join();
    Expect(success_response != nullptr, "internalWaitResponse must return a response on success");
    if (success_response)
    {
        Expect(
            !static_cast<ResponseInMessage*>(success_response.get())->IsErrorType(),
            "internalWaitResponse success must deliver a non-error response");
    }

    td_initialize::request timed_out_req;
    timed_out_req.id.set(902);
    auto const timed_out_response = point.internalWaitResponse(timed_out_req, 50);
    Expect(timed_out_response == nullptr, "internalWaitResponse must return null on timeout");

    td_initialize::request retry_req;
    retry_req.id.set(902);
    auto retry_future = point.send(retry_req);
    auto const retry_status = retry_future.wait_for(std::chrono::milliseconds(50));
    Expect(
        retry_status == lsp::future_status::timeout,
        "request id must be reusable after internalWaitResponse timeout");

    point.stop();

    auto bad_output = std::make_shared<StringOStream>();
    bad_output->set_bad(true);
    RemoteEndPoint bad_point(json_handler, endpoint, log);
    td_initialize::request send_failed_req;
    send_failed_req.id.set(903);
    auto const send_failed_response = bad_point.internalWaitResponse(send_failed_req, 0);
    Expect(send_failed_response == nullptr, "internalWaitResponse must return null when send fails");
}

void TestSendWithOnErrorCallback()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request success_req;
    success_req.id.set(1000);
    std::atomic<bool> success_response_called {false};
    std::atomic<bool> unexpected_error_called {false};
    point.send(
        success_req,
        [&](td_initialize::response const& rsp)
        {
            success_response_called.store(true, std::memory_order_relaxed);
            Expect(rsp.id == success_req.id, "send(onError) success handler must receive the matching response id");
        },
        [&](Rsp_Error const&)
        {
            unexpected_error_called.store(true, std::memory_order_relaxed);
        });
    WaitForOutputContaining(output_stream, "\"id\":1000");
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1000,"result":{"capabilities":{}}})"));
    for (int i = 0; i < 100 && !success_response_called.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Expect(success_response_called.load(std::memory_order_relaxed), "send(onError) must invoke success handler for success responses");
    Expect(!unexpected_error_called.load(std::memory_order_relaxed), "send(onError) must not invoke onError for success responses");

    td_initialize::request req;
    req.id.set(1001);
    std::atomic<bool> success_called {false};
    std::atomic<bool> error_called {false};
    lsErrorCodes captured_code = lsErrorCodes::UnknownErrorCode;
    std::string captured_message;

    point.send(
        req,
        [&](td_initialize::response const&)
        {
            success_called.store(true, std::memory_order_relaxed);
        },
        [&](Rsp_Error const& err)
        {
            error_called.store(true, std::memory_order_relaxed);
            captured_code = err.error.code;
            captured_message = err.error.message;
        });
    WaitForOutputContaining(output_stream, "\"id\":1001");
    input_stream->append(
        test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1001,"error":{"code":-32600,"message":"callback error"}})"));
    for (int i = 0; i < 100 && !error_called.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    point.stop();

    Expect(error_called.load(std::memory_order_relaxed), "send(onError) must invoke onError for error responses");
    Expect(!success_called.load(std::memory_order_relaxed), "send(onError) must not invoke success handler on error");
    Expect(captured_code == lsErrorCodes::InvalidRequest, "send(onError) must deliver the response error code");
    Expect(captured_message == "callback error", "send(onError) must deliver the response error message");

    auto bad_output = std::make_shared<StringOStream>();
    bad_output->set_bad(true);
    RemoteEndPoint bad_point(json_handler, endpoint, log);
    td_initialize::request failed_send_req;
    failed_send_req.id.set(1002);
    std::atomic<bool> send_failure_called {false};
    std::string send_failure_message;
    bad_point.send(
        failed_send_req,
        [&](td_initialize::response const&)
        {
        },
        [&](Rsp_Error const& err)
        {
            send_failure_called.store(true, std::memory_order_relaxed);
            send_failure_message = err.error.message;
        });
    Expect(send_failure_called.load(std::memory_order_relaxed), "send(onError) must invoke onError when send fails");
    Expect(
        send_failure_message == "Failed to send request.",
        "send(onError) must report failed send through onError");
}

void TestDelimitedRemoteEndPointRoundTrip()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":1101,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(MakeDelimitedInput(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Delimited);
    point.registerHandler(
        [](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":1101");
    point.stop();

    Expect(output.find("\"id\":1101") != std::string::npos, "delimited RemoteEndPoint must respond to delimited input");
    Expect(
        output.find("Content-Length:") != std::string::npos,
        "delimited RemoteEndPoint must still write standard framed output");
}

void TestDispatchUnknownNotificationReturnsFalse()
{
    std::string const notification = R"({"jsonrpc":"2.0","method":"unknown/notification","params":{}})";
    auto const output = ProcessSingleInput(test::MakeLspFrame(notification));

    Expect(output.empty(), "unknown notification must not produce output");
}

void TestDispatchResponseParseFailure()
{
    DummyLog log;
    auto json_handler = std::make_shared<MessageJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.startProcessingMessages(input_stream, output_stream);

    td_initialize::request req;
    req.id.set(1201);
    std::unique_ptr<LspMessage> response;
    std::thread waiter(
        [&]()
        {
            response = point.internalWaitResponse(req, 500);
        });
    WaitForOutputContaining(output_stream, "\"id\":1201");
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1201,"result":{"capabilities":{}}})"));
    waiter.join();
    point.stop();

    Expect(response == nullptr, "response parse failure must not deliver a parsed message");
}

void TestRegisterHandlerReturnsErrorResponse()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":1301,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            Rsp_Error error;
            error.id = req.id;
            error.error.code = lsErrorCodes::InternalError;
            error.error.message = "handler returned error";
            return error;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"code\":-32603");
    point.stop();

    Expect(output.find("\"id\":1301") != std::string::npos, "handler error response must preserve request id");
    Expect(output.find("\"code\":-32603") != std::string::npos, "handler error response must serialize error code");
    Expect(
        output.find("handler returned error") != std::string::npos,
        "handler error response must serialize error message");
}

void TestCancelRequestWhenNotWorkingOrUnknownId()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    RemoteEndPoint point(json_handler, endpoint, log);

    lsRequestId unknown_id;
    unknown_id.set(404);
    Expect(!point.cancelRequest(unknown_id), "cancelRequest must return false when endpoint is not working");

    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();
    point.startProcessingMessages(input_stream, output_stream);

    lsRequestId not_pending_id;
    not_pending_id.set(405);
    Expect(!point.cancelRequest(not_pending_id), "cancelRequest must return false for unknown request id");

    point.stop();
    Expect(!point.cancelRequest(not_pending_id), "cancelRequest must return false after stop");
}

void TestSendNotificationBeforeStartDoesNotCrash()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    RemoteEndPoint point(json_handler, endpoint, log);

    Notify_Exit::notify notification;
    point.send(notification);

    Expect(!point.isWorking(), "sending a notification before start must not mark endpoint as working");
}

void TestRemoteEndPointRestartTwice()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<int> handled_count {0};
    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [&](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            handled_count.fetch_add(1, std::memory_order_relaxed);
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });

    Expect(!point.isWorking(), "RemoteEndPoint must not be working before start");
    auto first_input = std::make_shared<StringIStream>(
        test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1401,"method":"initialize","params":{}})"));
    point.startProcessingMessages(first_input, output_stream);
    Expect(point.isWorking(), "RemoteEndPoint must report working after start");
    WaitForOutputContaining(output_stream, "\"id\":1401");
    point.stop();
    Expect(!point.isWorking(), "RemoteEndPoint must not report working after stop");

    auto second_input = std::make_shared<StringIStream>(
        test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1402,"method":"initialize","params":{}})"));
    point.startProcessingMessages(second_input, output_stream);
    Expect(point.isWorking(), "RemoteEndPoint must report working after restart");
    WaitForOutputContaining(output_stream, "\"id\":1402");
    point.stop();
    Expect(!point.isWorking(), "RemoteEndPoint must not report working after second stop");

    Expect(handled_count.load(std::memory_order_relaxed) == 2, "RemoteEndPoint must handle requests across two restarts");
}

void TestRegisterHandlerThrowsRequestError()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":1501,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [](td_initialize::request const&) -> td_initialize::response
        {
            throw lsp::RequestError(lsErrorCodes::InvalidParams, "invalid initialize params");
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"code\":-32602");
    point.stop();

    Expect(output.find("\"id\":1501") != std::string::npos, "RequestError response must preserve request id");
    Expect(
        output.find("invalid initialize params") != std::string::npos,
        "RequestError response must serialize error message");
}

void TestRegisterHandlerThrowsStdException()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":1505,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [](td_initialize::request const&) -> td_initialize::response
        {
            throw std::runtime_error("sync handler failed");
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "sync handler failed");
    point.stop();

    Expect(output.find("\"id\":1505") != std::string::npos, "std::exception response must preserve request id");
    Expect(output.find("\"code\":-32603") != std::string::npos, "std::exception must become InternalError");
    Expect(output.find("sync handler failed") != std::string::npos, "std::exception message must be serialized");
}

void TestAsyncRegisterHandlerCompletesLater()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":1502,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [](td_initialize::request const& req) -> lsp::future<td_initialize::response>
        {
            auto promise = std::make_shared<lsp::promise<td_initialize::response>>();
            auto future = promise->get_future();
            std::thread(
                [promise, id = req.id]()
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    td_initialize::response rsp;
                    rsp.id = id;
                    rsp.result.capabilities.hoverProvider = true;
                    promise->set_value(std::move(rsp));
                }
            ).detach();
            return future;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"hoverProvider\":true");
    point.stop();

    Expect(output.find("\"id\":1502") != std::string::npos, "async handler response must preserve request id");
    Expect(
        output.find("\"hoverProvider\":true") != std::string::npos,
        "async handler response must include handler result");
}

void TestAsyncRegisterHandlerReturnsErrorResult()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":1503,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [](td_initialize::request const&) -> lsp::future<lsp::ResponseOrError<td_initialize::response>>
        {
            auto promise = std::make_shared<lsp::promise<lsp::ResponseOrError<td_initialize::response>>>();
            auto future = promise->get_future();
            std::thread(
                [promise]()
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    Rsp_Error error;
                    error.error.code = lsErrorCodes::InvalidParams;
                    error.error.message = "async invalid params";
                    promise->set_value(lsp::ResponseOrError<td_initialize::response>(std::move(error)));
                }
            ).detach();
            return future;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "async invalid params");
    point.stop();

    Expect(output.find("\"id\":1503") != std::string::npos, "async error response must preserve request id");
    Expect(
        output.find("\"code\":-32602") != std::string::npos,
        "async error response must serialize error code");
}

void TestAsyncRegisterHandlerWithMonitorCompletesLater()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    std::string const request = R"({"jsonrpc":"2.0","id":1504,"method":"initialize","params":{}})";
    auto input_stream = std::make_shared<StringIStream>(test::MakeLspFrame(request));
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<bool> monitor_seen {false};
    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::future<td_initialize::response>
        {
            auto promise = std::make_shared<lsp::promise<td_initialize::response>>();
            auto future = promise->get_future();
            std::thread(
                [promise, id = req.id, monitor, &monitor_seen]()
                {
                    monitor_seen.store(monitor && monitor(), std::memory_order_relaxed);
                    td_initialize::response rsp;
                    rsp.id = id;
                    promise->set_value(std::move(rsp));
                }
            ).detach();
            return future;
        });
    point.startProcessingMessages(input_stream, output_stream);

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":1504");
    point.stop();

    Expect(output.find("\"id\":1504") != std::string::npos, "async cancellable handler must produce a response");
    Expect(!monitor_seen.load(std::memory_order_relaxed), "async handler must receive a live cancel monitor");
}

void TestAsyncHandlerDoesNotBlockDispatchWorker()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<int> handled_count {0};
    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point.registerHandler(
        [&](td_initialize::request const& req) -> lsp::future<td_initialize::response>
        {
            int const count = handled_count.fetch_add(1, std::memory_order_relaxed) + 1;
            auto promise = std::make_shared<lsp::promise<td_initialize::response>>();
            auto future = promise->get_future();
            if (count == 2)
            {
                td_initialize::response rsp;
                rsp.id = req.id;
                promise->set_value(std::move(rsp));
            }
            return future;
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1601,"method":"initialize","params":{}})"));
    for (int i = 0; i < 100 && handled_count.load(std::memory_order_relaxed) < 1; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1602,"method":"initialize","params":{}})"));

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":1602");
    point.stop();

    Expect(handled_count.load(std::memory_order_relaxed) >= 2, "pending async future must not block dispatch worker");
    Expect(output.find("\"id\":1602") != std::string::npos, "second request must complete while first future is pending");
}

void TestMultiplePendingAsyncFuturesBothComplete()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<bool> release {false};
    RemoteEndPoint point(json_handler, endpoint, log);
    point.registerHandler(
        [&](td_initialize::request const& req) -> lsp::future<td_initialize::response>
        {
            auto promise = std::make_shared<lsp::promise<td_initialize::response>>();
            auto future = promise->get_future();
            std::thread(
                [promise, id = req.id, &release]()
                {
                    for (int i = 0; i < 200 && !release.load(std::memory_order_relaxed); ++i)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    td_initialize::response rsp;
                    rsp.id = id;
                    promise->set_value(std::move(rsp));
                }
            ).detach();
            return future;
        });
    point.startProcessingMessages(input_stream, output_stream);

    // Queue two unready futures at the same time, then release both. The
    // completion loop must keep rotating through pending futures and answer
    // each request once its future becomes ready.
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1605,"method":"initialize","params":{}})"));
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1606,"method":"initialize","params":{}})"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    release.store(true, std::memory_order_relaxed);

    std::string const output = WaitForOutputContainingAll(output_stream, {"\"id\":1605", "\"id\":1606"});
    point.stop();

    Expect(output.find("\"id\":1605") != std::string::npos, "first pending async future must complete");
    Expect(output.find("\"id\":1606") != std::string::npos, "second pending async future must complete");
}

void TestAsyncCancellationAfterHandlerReturns()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<bool> handler_started {false};
    std::atomic<bool> saw_cancel {false};
    RemoteEndPoint point(json_handler, endpoint, log, lsp::JSONStreamStyle::Standard, 1);
    point.registerHandler(
        [&](td_initialize::request const& req, CancelMonitor const& monitor) -> lsp::future<td_initialize::response>
        {
            auto promise = std::make_shared<lsp::promise<td_initialize::response>>();
            auto future = promise->get_future();
            handler_started.store(true, std::memory_order_relaxed);
            std::thread(
                [promise, id = req.id, monitor, &saw_cancel]()
                {
                    for (int i = 0; i < 100; ++i)
                    {
                        if (monitor && monitor())
                        {
                            saw_cancel.store(true, std::memory_order_relaxed);
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    td_initialize::response rsp;
                    rsp.id = id;
                    promise->set_value(std::move(rsp));
                }
            ).detach();
            return future;
        });
    point.startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1603,"method":"initialize","params":{}})"));
    for (int i = 0; i < 100 && !handler_started.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":1603}})"));

    std::string const output = WaitForOutputContaining(output_stream, "\"id\":1603");
    point.stop();

    Expect(output.find("\"id\":1603") != std::string::npos, "cancelled async request must still be able to respond");
    Expect(saw_cancel.load(std::memory_order_relaxed), "async handler monitor must observe cancel after handler returns");
}

void TestStopDoesNotWaitForUnfinishedAsyncFuture()
{
    DummyLog log;
    auto json_handler = std::make_shared<lsp::ProtocolJsonHandler>();
    auto endpoint = std::make_shared<GenericEndpoint>(log);
    auto input_stream = std::make_shared<FeedableIStream>();
    auto output_stream = std::make_shared<StringOStream>();

    std::atomic<bool> handler_started {false};
    auto point = new RemoteEndPoint(json_handler, endpoint, log);
    point->registerHandler(
        [&](td_initialize::request const&) -> lsp::future<td_initialize::response>
        {
            auto promise = std::make_shared<lsp::promise<td_initialize::response>>();
            handler_started.store(true, std::memory_order_relaxed);
            return promise->get_future();
        });
    point->startProcessingMessages(input_stream, output_stream);

    input_stream->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1604,"method":"initialize","params":{}})"));
    for (int i = 0; i < 100 && !handler_started.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::atomic<bool> stopped {false};
    std::thread stopper(
        [&]()
        {
            point->stop();
            stopped.store(true, std::memory_order_relaxed);
        });
    for (int i = 0; i < 100 && !stopped.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Expect(stopped.load(std::memory_order_relaxed), "stop must not wait for an unfinished async handler future");
    if (stopped.load(std::memory_order_relaxed))
    {
        stopper.join();
        delete point;
    }
    else
    {
        stopper.detach();
    }
}

void TestFutureGetSupportsMoveOnlyValue()
{
    lsp::promise<MoveOnlyFutureValue> promise;
    auto future = promise.get_future();

    promise.set_value(MoveOnlyFutureValue(42));
    MoveOnlyFutureValue value = future.get();

    Expect(value.value == 42, "future::get must move move-only values out of the shared state");
}
} // namespace

int main()
{
    TestDuplicateRequestIdReturnsError();
    TestWaitResponseTimeoutClearsPendingRequest();
    TestBadOutputStreamReturnsError();
    TestStopCompletesPendingRequestWithError();
    TestStopCancelsRunningRequestHandlers();
    TestStopFromNotificationHandlerDoesNotDeadlock();
    TestAsyncFutureCompletedAfterStopIsDroppedSafely();
    TestUnregisteredRequestReturnsMethodNotFound();
    TestUnregisteredRequestPreservesStringIdInMethodNotFound();
    TestRegisteredRequestDoesNotReturnMethodNotFound();
    TestMalformedJsonDoesNotCrash();
    TestWrongJsonRpcVersionIsRejected();
    TestNonStringJsonRpcVersionIsRejected();
    TestNonStringMethodIsRejected();
    TestRequestMissingParamsDoesNotCrashEndpoint();
    TestResponseWithNoMatchingRequestIsIgnored();
    TestMessageWithIdAndMethodUsesRequestPath();
    TestMessageWithNoMethodNoResultIsRejected();
    TestSendRequestAndReceiveSuccessResponse();
    TestSendRequestAndReceiveErrorResponse();
    TestDuplicateResponseCompletesFutureOnce();
    TestSendRequestAndReceiveSuccessResponseWithStringId();
    TestLateResponseAfterTimeoutIsIgnored();
    TestCreateRequestAssignsMonotonicIds();
    TestCancelRequestSendsCancelNotification();
    TestPendingCancelArrivingBeforeRequestIsNotLost();
    TestLateCancelAfterCompletedRequestDoesNotCancelReusedId();
    TestOutOfOrderPendingCancelAppliesToReusedRequestId();
    TestStopClearsPendingCancelRequests();
    TestRunningRequestObservesCancellation();
    TestBurstRequestsAreAllDispatchedAndResponded();
    TestMultipleWorkersProcessRequestsConcurrently();
    TestConcurrentSendWritesCompleteFrames();
    TestNotificationHandlerReceivesNotification();
    TestThrowingNotificationHandlerDoesNotStopEndpoint();
    TestConditionTimedWaitReturnsNotifiedValue();
    TestConditionTimedWaitStillTimesOut();
    TestConditionWaitZeroBlocksUntilNotify();
    TestInternalWaitResponseSuccessTimeoutAndSendFailure();
    TestSendWithOnErrorCallback();
    TestDelimitedRemoteEndPointRoundTrip();
    TestDispatchUnknownNotificationReturnsFalse();
    TestDispatchResponseParseFailure();
    TestRegisterHandlerReturnsErrorResponse();
    TestRegisterHandlerThrowsRequestError();
    TestRegisterHandlerThrowsStdException();
    TestAsyncRegisterHandlerCompletesLater();
    TestAsyncRegisterHandlerReturnsErrorResult();
    TestAsyncRegisterHandlerWithMonitorCompletesLater();
    TestAsyncHandlerDoesNotBlockDispatchWorker();
    TestMultiplePendingAsyncFuturesBothComplete();
    TestAsyncCancellationAfterHandlerReturns();
    TestStopDoesNotWaitForUnfinishedAsyncFuture();
    TestFutureGetSupportsMoveOnlyValue();
    TestCancelRequestWhenNotWorkingOrUnknownId();
    TestSendNotificationBeforeStartDoesNotCrash();
    TestRemoteEndPointRestartTwice();

    return test::Failures() == 0 ? 0 : 1;
}
