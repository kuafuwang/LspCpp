#include "LibLsp/JsonRpc/Endpoint.h"
#include "LibLsp/JsonRpc/MessageJsonHandler.h"
#include "LibLsp/JsonRpc/json.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/general/exit.h"
#include "LibLsp/lsp/general/initialize.h"
#include "test_helpers.h"

#include <rapidjson/document.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using test::DummyLog;
using test::Expect;

JsonReader MakeReader(rapidjson::Document& document, char const* json)
{
    document.Parse(json);
    return JsonReader(&document);
}

template<typename Fn>
bool ThrowsInvalidArgument(Fn fn)
{
    try
    {
        fn();
    }
    catch (std::invalid_argument const&)
    {
        return true;
    }
    catch (...)
    {
    }
    return false;
}

void TestGenericEndpointDispatchesRegisteredHandlers()
{
    DummyLog log;
    GenericEndpoint endpoint(log);

    bool request_called = false;
    endpoint.registerRequestHandler(
        "initialize",
        [&](std::unique_ptr<LspMessage> msg)
        {
            request_called = msg && std::string(msg->GetMethodType()) == "initialize";
            return true;
        });

    std::unique_ptr<LspMessage> request(new td_initialize::request());
    bool const request_result = endpoint.onRequest(std::move(request));
    Expect(request_result, "registered request handler must return true");
    Expect(request_called, "registered request handler must be called");

    bool notify_called = false;
    endpoint.registerNotifyHandler(
        "exit",
        [&](std::unique_ptr<LspMessage> msg)
        {
            notify_called = msg && std::string(msg->GetMethodType()) == "exit";
            return true;
        });

    std::unique_ptr<LspMessage> notify(new Notify_Exit::notify());
    bool const notify_result = endpoint.notify(std::move(notify));
    Expect(notify_result, "registered notification handler must return true");
    Expect(notify_called, "registered notification handler must be called");

    bool response_called = false;
    endpoint.method2response["initialize"] = [&](std::unique_ptr<LspMessage> msg) {
        response_called = msg != nullptr;
        return true;
    };

    std::unique_ptr<LspMessage> response(new td_initialize::response());
    bool const response_result = endpoint.onResponse("initialize", std::move(response));
    Expect(response_result, "registered response handler must return true");
    Expect(response_called, "registered response handler must be called");
}

void TestGenericEndpointRejectsMissingHandlers()
{
    DummyLog log;
    GenericEndpoint endpoint(log);

    std::unique_ptr<LspMessage> request(new td_initialize::request());
    Expect(!endpoint.onRequest(std::move(request)), "missing request handler must return false");

    std::unique_ptr<LspMessage> notify(new Notify_Exit::notify());
    Expect(!endpoint.notify(std::move(notify)), "missing notification handler must return false");

    std::unique_ptr<LspMessage> response(new td_initialize::response());
    Expect(!endpoint.onResponse("initialize", std::move(response)), "missing response handler must return false");
}

void TestMessageJsonHandlerParsesRegisteredMethods()
{
    MessageJsonHandler handler;
    rapidjson::Document document;
    JsonReader reader = MakeReader(document, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");

    handler.SetRequestJsonHandler(
        "initialize",
        [](Reader&)
        {
            return std::unique_ptr<LspMessage>(new td_initialize::request());
        });
    auto request = handler.parseRequstMessage("initialize", reader);
    Expect(request != nullptr, "registered request parser must return a message");
    Expect(
        handler.parseRequstMessage("unknown", reader) == nullptr,
        "unknown request parser must return null");

    handler.SetNotificationJsonHandler(
        "exit",
        [](Reader&)
        {
            return std::unique_ptr<LspMessage>(new Notify_Exit::notify());
        });
    auto notify = handler.parseNotificationMessage("exit", reader);
    Expect(notify != nullptr, "registered notification parser must return a message");
    Expect(
        handler.parseNotificationMessage("unknown", reader) == nullptr,
        "unknown notification parser must return null");

    handler.SetResponseJsonHandler(
        "initialize",
        [](Reader&)
        {
            return std::unique_ptr<LspMessage>(new td_initialize::response());
        });
    auto response = handler.parseResponseMessage("initialize", reader);
    Expect(response != nullptr, "registered response parser must return a message");
    Expect(
        handler.parseResponseMessage("unknown", reader) == nullptr,
        "unknown response parser must return null");
}

void TestMessageJsonHandlerResolveResponseSkipsThrowingParsers()
{
    MessageJsonHandler handler;
    rapidjson::Document document;
    JsonReader reader = MakeReader(document, R"({"jsonrpc":"2.0","id":1,"result":{}})");

    handler.SetResponseJsonHandler(
        "a.throwing",
        [](Reader&) -> std::unique_ptr<LspMessage>
        {
            throw std::invalid_argument("expected test exception");
        });
    handler.SetResponseJsonHandler(
        "b.success",
        [](Reader&)
        {
            return std::unique_ptr<LspMessage>(new td_initialize::response());
        });

    std::pair<std::string, std::unique_ptr<LspMessage>> resolved;
    bool const result = handler.resovleResponseMessage(reader, resolved);
    Expect(result, "response resolver must succeed when a later parser accepts the message");
    Expect(resolved.first == "b.success", "response resolver must report the successful method");
    Expect(resolved.second != nullptr, "response resolver must return parsed message");
}

void TestPrimitiveReaderRejectsWrongTypes()
{
    rapidjson::Document string_document;
    JsonReader string_reader = MakeReader(string_document, R"("not-an-int")");
    int int_value = 0;
    Expect(
        ThrowsInvalidArgument([&]() { Reflect(string_reader, int_value); }),
        "int reader must reject JSON string values");

    rapidjson::Document int_document;
    JsonReader int_reader = MakeReader(int_document, "123");
    std::string string_value;
    Expect(
        ThrowsInvalidArgument([&]() { Reflect(int_reader, string_value); }),
        "string reader must reject JSON integer values");

    rapidjson::Document bool_document;
    JsonReader bool_reader = MakeReader(bool_document, "true");
    double double_value = 0.0;
    Expect(
        ThrowsInvalidArgument([&]() { Reflect(bool_reader, double_value); }),
        "double reader must reject JSON bool values");
}
} // namespace

int main()
{
    TestGenericEndpointDispatchesRegisteredHandlers();
    TestGenericEndpointRejectsMissingHandlers();
    TestMessageJsonHandlerParsesRegisteredMethods();
    TestMessageJsonHandlerResolveResponseSkipsThrowingParsers();
    TestPrimitiveReaderRejectsWrongTypes();

    return test::Failures() == 0 ? 0 : 1;
}
