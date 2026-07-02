#include "LibLsp/LspCpp.h"
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

void TestLanguageServerAliasConstructsUsableSession()
{
    lsp::LanguageServer server;

    Expect(server.protocolJsonHandler() != nullptr, "LanguageServer alias must expose ProtocolJsonHandler");
    Expect(server.localEndpoint() != nullptr, "LanguageServer alias must expose GenericEndpoint");
    Expect(
        server.protocolJsonHandler()->GetRequestJsonHandler(td_initialize::request::kMethodInfo) != nullptr,
        "LanguageServer alias must keep standard protocol registrations");
}

} // namespace

int main()
{
    TestLanguageSessionInitializeRoundTrip();
    TestLanguageSessionNotificationHandler();
    TestLanguageSessionEndpointCanSendNotifications();
    TestLanguageServerAliasConstructsUsableSession();
    return test::Failures() == 0 ? 0 : 1;
}
