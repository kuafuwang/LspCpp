#include "LibLsp/JsonRpc/Endpoint.h"
#include "LibLsp/JsonRpc/Condition.h"
#include "LibLsp/JsonRpc/RemoteEndPoint.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/general/initialize.h"
#include "test_helpers.h"

#include <chrono>
#include <memory>
#include <thread>

namespace
{
using test::DummyLog;
using test::Expect;
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
    TestConditionTimedWaitReturnsNotifiedValue();
    TestConditionTimedWaitStillTimesOut();

    return test::Failures() == 0 ? 0 : 1;
}
