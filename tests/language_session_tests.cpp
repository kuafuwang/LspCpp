#include "LibLsp/LspCpp.h"
#include "LibLsp/JsonRpc/RequestError.h"
#include "LibLsp/lsp/general/exit.h"
#include "LibLsp/lsp/general/shutdown.h"
#include "LibLsp/lsp/windows/MessageNotify.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace
{
using test::Expect;
using test::FeedableIStream;
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

void TestLanguageSessionInitializeRoundTrip()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);
    auto input = std::make_shared<FeedableIStream>();
    auto output = std::make_shared<StringOStream>();
    std::atomic<bool> handled {false};

    session.on(
        [&](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            handled.store(true, std::memory_order_relaxed);
            td_initialize::response rsp;
            rsp.id = req.id;
            rsp.result.capabilities.hoverProvider = true;
            return rsp;
        });
    session.start(input, output);

    input->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":"init-session","method":"initialize","params":{}})"));
    std::string const response = WaitForOutputContaining(output, R"("id":"init-session")");
    session.stop();

    Expect(handled.load(std::memory_order_relaxed), "LanguageSession must dispatch initialize through on()");
    Expect(response.find(R"("id":"init-session")") != std::string::npos, "LanguageSession response must preserve id");
    Expect(
        response.find("\"hoverProvider\":true") != std::string::npos,
        "LanguageSession response must include handler result");
}

void TestLanguageSessionNotificationHandler()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);
    auto input = std::make_shared<FeedableIStream>();
    auto output = std::make_shared<StringOStream>();
    std::atomic<bool> notified {false};

    session.on(
        [&](Notify_Exit::notify const&)
        {
            notified.store(true, std::memory_order_relaxed);
        });
    session.start(input, output);

    input->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"exit","params":{}})"));
    for (int i = 0; i < 100 && !notified.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    session.stop();

    Expect(notified.load(std::memory_order_relaxed), "LanguageSession must dispatch notifications through on()");
}

void TestLanguageSessionEndpointCanSendNotifications()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);
    auto input = std::make_shared<FeedableIStream>();
    auto output = std::make_shared<StringOStream>();
    session.start(input, output);

    Notify_LogMessage::notify notify;
    notify.params.type = lsMessageType::Log;
    notify.params.message = "session-log";
    session.endpoint().send(notify);

    std::string const sent = WaitForOutputContaining(output, "session-log");
    session.stop();

    Expect(
        sent.find(R"("method":"window/logMessage")") != std::string::npos,
        "LanguageSession endpoint() must expose outbound notification sends");
    Expect(sent.find("session-log") != std::string::npos, "LanguageSession outbound notification must include payload");
}

void TestLanguageSessionCanOverrideBuiltinRequestParser()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);
    auto input = std::make_shared<FeedableIStream>();
    auto output = std::make_shared<StringOStream>();
    std::atomic<bool> parser_used {false};
    std::atomic<bool> handled {false};

    session.overrideRequestParser(
        td_initialize::request::kMethodInfo,
        [&](Reader& visitor)
        {
            parser_used.store(true, std::memory_order_relaxed);
            return td_initialize::request::ReflectReader(visitor);
        });
    session.on(
        [&](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            handled.store(true, std::memory_order_relaxed);
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    session.start(input, output);

    input->append(
        test::MakeLspFrame(R"({"jsonrpc":"2.0","id":"custom-parser","method":"initialize","params":{}})")
    );
    std::string const response = WaitForOutputContaining(output, R"("id":"custom-parser")");
    session.stop();

    Expect(parser_used.load(std::memory_order_relaxed), "custom request parser must override the built-in parser");
    Expect(handled.load(std::memory_order_relaxed), "overridden request parser must still dispatch to the handler");
    Expect(
        response.find(R"("id":"custom-parser")") != std::string::npos,
        "response must preserve the request id parsed by the custom parser");
}

void TestLanguageServerAliasConstructsUsableSession()
{
    lsp::LanguageServer server;

    Expect(server.protocolJsonHandler() != nullptr, "LanguageServer alias must expose ProtocolJsonHandler");
    Expect(server.localEndpoint() != nullptr, "LanguageServer alias must expose GenericEndpoint");
    Expect(
        server.protocolJsonHandler()->GetRequestJsonHandler(td_initialize::request::kMethodInfo) != nullptr,
        "LanguageServer alias must keep standard protocol registrations");
}

void TestLanguageSessionShutdownSequence()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);
    auto input = std::make_shared<FeedableIStream>();
    auto output = std::make_shared<StringOStream>();
    std::atomic<bool> shutdown_handled {false};
    std::atomic<bool> exit_notified {false};

    session.on(
        [&](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response>
        {
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    session.on(
        [&](td_shutdown::request const& req) -> lsp::ResponseOrError<td_shutdown::response>
        {
            shutdown_handled.store(true, std::memory_order_relaxed);
            td_shutdown::response rsp;
            rsp.id = req.id;
            return rsp;
        });
    session.on(
        [&](Notify_Exit::notify const&)
        {
            exit_notified.store(true, std::memory_order_relaxed);
        });
    session.start(input, output);

    input->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
    std::string const init_response = WaitForOutputContaining(output, "\"id\":1");

    input->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":null})"));
    std::string const shutdown_response = WaitForOutputContaining(output, "\"id\":2");

    input->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","method":"exit","params":{}})"));
    for (int i = 0; i < 100 && !exit_notified.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    session.stop();

    Expect(init_response.find("\"id\":1") != std::string::npos, "initialize must receive a response before shutdown");
    Expect(shutdown_handled.load(std::memory_order_relaxed), "LanguageSession must dispatch shutdown request handler");
    Expect(
        shutdown_response.find("\"id\":2") != std::string::npos,
        "LanguageSession shutdown response must preserve request id");
    Expect(exit_notified.load(std::memory_order_relaxed), "LanguageSession must dispatch exit after shutdown");
}

void TestLanguageSessionThrowsRequestError()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);
    auto input = std::make_shared<FeedableIStream>();
    auto output = std::make_shared<StringOStream>();

    session.on(
        [](td_initialize::request const&) -> td_initialize::response
        {
            throw lsp::RequestError(lsErrorCodes::ServerNotInitialized, "server not initialized");
        });
    session.start(input, output);

    input->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":"session-error","method":"initialize","params":{}})"));
    std::string const response = WaitForOutputContaining(output, "\"code\":-32002");
    session.stop();

    Expect(
        response.find(R"("id":"session-error")") != std::string::npos,
        "LanguageSession RequestError response must preserve request id");
    Expect(
        response.find("server not initialized") != std::string::npos,
        "LanguageSession RequestError response must serialize error message");
}

void TestLanguageSessionAsyncHandlerCompletesLater()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);
    auto input = std::make_shared<FeedableIStream>();
    auto output = std::make_shared<StringOStream>();

    session.on(
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
    session.start(input, output);

    input->append(test::MakeLspFrame(R"({"jsonrpc":"2.0","id":"session-async","method":"initialize","params":{}})"));
    std::string const response = WaitForOutputContaining(output, "\"hoverProvider\":true");
    session.stop();

    Expect(
        response.find(R"("id":"session-async")") != std::string::npos,
        "LanguageSession async handler response must preserve request id");
    Expect(
        response.find("\"hoverProvider\":true") != std::string::npos,
        "LanguageSession async handler response must include handler result");
}

} // namespace

int main()
{
    TestLanguageSessionInitializeRoundTrip();
    TestLanguageSessionNotificationHandler();
    TestLanguageSessionEndpointCanSendNotifications();
    TestLanguageSessionCanOverrideBuiltinRequestParser();
    TestLanguageServerAliasConstructsUsableSession();
    TestLanguageSessionShutdownSequence();
    TestLanguageSessionThrowsRequestError();
    TestLanguageSessionAsyncHandlerCompletesLater();
    return test::Failures() == 0 ? 0 : 1;
}
