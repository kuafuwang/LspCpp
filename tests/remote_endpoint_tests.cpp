#include "LibLsp/JsonRpc/Endpoint.h"
#include "LibLsp/JsonRpc/RemoteEndPoint.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/general/initialize.h"
#include "test_helpers.h"

#include <chrono>
#include <memory>

namespace
{
using test::DummyLog;
using test::Expect;
using test::StringIStream;
using test::StringOStream;

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
} // namespace

int main()
{
    TestDuplicateRequestIdReturnsError();
    TestWaitResponseTimeoutClearsPendingRequest();
    TestBadOutputStreamReturnsError();
    TestStopCompletesPendingRequestWithError();

    return test::Failures() == 0 ? 0 : 1;
}
