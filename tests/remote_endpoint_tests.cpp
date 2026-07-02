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
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace
{
using test::DummyLog;
using test::Expect;
using test::FeedableIStream;
using test::StringIStream;
using test::StringOStream;

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
} // namespace

int main()
{
    TestDuplicateRequestIdReturnsError();
    TestWaitResponseTimeoutClearsPendingRequest();
    TestBadOutputStreamReturnsError();
    TestStopCompletesPendingRequestWithError();
    TestUnregisteredRequestReturnsMethodNotFound();
    TestUnregisteredRequestPreservesStringIdInMethodNotFound();
    TestRegisteredRequestDoesNotReturnMethodNotFound();
    TestMalformedJsonDoesNotCrash();
    TestWrongJsonRpcVersionIsRejected();
    TestResponseWithNoMatchingRequestIsIgnored();
    TestMessageWithIdAndMethodUsesRequestPath();
    TestMessageWithNoMethodNoResultIsRejected();
    TestSendRequestAndReceiveSuccessResponse();
    TestSendRequestAndReceiveErrorResponse();
    TestSendRequestAndReceiveSuccessResponseWithStringId();
    TestLateResponseAfterTimeoutIsIgnored();
    TestCreateRequestAssignsMonotonicIds();
    TestCancelRequestSendsCancelNotification();
    TestPendingCancelArrivingBeforeRequestIsNotLost();
    TestLateCancelAfterCompletedRequestDoesNotCancelReusedId();
    TestStopClearsPendingCancelRequests();
    TestRunningRequestObservesCancellation();
    TestBurstRequestsAreAllDispatchedAndResponded();
    TestMultipleWorkersProcessRequestsConcurrently();
    TestConcurrentSendWritesCompleteFrames();
    TestNotificationHandlerReceivesNotification();
    TestConditionTimedWaitReturnsNotifiedValue();
    TestConditionTimedWaitStillTimesOut();

    return test::Failures() == 0 ? 0 : 1;
}
