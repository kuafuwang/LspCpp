#include "LibLsp/JsonRpc/json.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/AbsolutePath.h"
#include "LibLsp/lsp/general/initialize.h"
#include "LibLsp/lsp/location_type.h"
#include "LibLsp/lsp/lsp_completion.h"
#include "LibLsp/lsp/lsp_diagnostic.h"
#include "LibLsp/lsp/lsTextEdit.h"
#include "LibLsp/lsp/lsWorkspaceEdit.h"
#include "LibLsp/lsp/textDocument/code_action.h"
#include "LibLsp/lsp/textDocument/completion.h"
#include "LibLsp/lsp/textDocument/declaration_definition.h"
#include "LibLsp/lsp/textDocument/did_change.h"
#include "LibLsp/lsp/textDocument/did_close.h"
#include "LibLsp/lsp/textDocument/did_open.h"
#include "LibLsp/lsp/textDocument/document_symbol.h"
#include "LibLsp/lsp/textDocument/formatting.h"
#include "LibLsp/lsp/textDocument/hover.h"
#include "LibLsp/lsp/textDocument/publishDiagnostics.h"
#include "LibLsp/lsp/textDocument/references.h"
#include "LibLsp/lsp/textDocument/rename.h"
#include "LibLsp/lsp/workspace/symbol.h"
#include "test_helpers.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
using test::Expect;

template<typename T>
std::string SerializeJson(T value)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    JsonWriter json_writer(&writer);
    Reflect(json_writer, value);
    return buffer.GetString();
}

template<typename T>
T RoundTrip(T value)
{
    std::string const json = SerializeJson(value);
    rapidjson::Document document;
    document.Parse(json.c_str());
    JsonReader reader(&document);
    T result;
    Reflect(reader, result);
    return result;
}

JsonReader MakeReader(rapidjson::Document& document, char const* json)
{
    document.Parse(json);
    return JsonReader(&document);
}

lsDocumentUri MakeDocumentUri(char const* uri)
{
    lsDocumentUri document_uri;
    document_uri.raw_uri_ = uri;
    return document_uri;
}

void ExpectParsesRequest(lsp::ProtocolJsonHandler& handler, MethodType method, char const* json, char const* message)
{
    rapidjson::Document document;
    JsonReader reader = MakeReader(document, json);
    Expect(handler.parseRequstMessage(method, reader) != nullptr, message);
}

void ExpectParsesNotification(lsp::ProtocolJsonHandler& handler, MethodType method, char const* json, char const* message)
{
    rapidjson::Document document;
    JsonReader reader = MakeReader(document, json);
    Expect(handler.parseNotificationMessage(method, reader) != nullptr, message);
}

void TestPositionRangeLocationAndTextEditRoundTrip()
{
    lsPosition const position = RoundTrip(lsPosition(3, 14));
    Expect(position.line == 3 && position.character == 14, "lsPosition must round-trip line and character");

    lsRange range(lsPosition(1, 2), lsPosition(3, 4));
    lsRange const range_copy = RoundTrip(range);
    Expect(range_copy == range, "lsRange must round-trip start and end positions");

    lsLocation location(MakeDocumentUri("file:///tmp/main.cpp"), range);
    lsLocation const location_copy = RoundTrip(location);
    Expect(location_copy == location, "lsLocation must round-trip URI and range");

    lsTextEdit edit;
    edit.range = range;
    edit.newText = "replacement";
    edit.annotationId = std::string("edit-1");
    lsTextEdit const edit_copy = RoundTrip(edit);
    Expect(edit_copy.range == edit.range, "lsTextEdit range must round-trip");
    Expect(edit_copy.newText == edit.newText, "lsTextEdit newText must round-trip");
    Expect(edit_copy.annotationId && *edit_copy.annotationId == "edit-1", "text edit annotationId must survive round-trip");
}

void TestWorkspaceEditAndDiagnosticRoundTrip()
{
    lsTextEdit edit;
    edit.range = lsRange(lsPosition(0, 0), lsPosition(0, 3));
    edit.newText = "bar";

    lsWorkspaceEdit workspace_edit;
    workspace_edit.changes = std::map<std::string, std::vector<lsTextEdit>>();
    (*workspace_edit.changes)["file:///tmp/a.cpp"].push_back(edit);
    workspace_edit.metadata = lsWorkspaceEditMetadata();
    workspace_edit.metadata->isRefactoring = true;

    lsWorkspaceEdit const workspace_copy = RoundTrip(workspace_edit);
    Expect(workspace_copy.changes.has_value(), "workspace edit changes must survive round-trip");
    Expect(
        workspace_copy.changes && workspace_copy.changes->at("file:///tmp/a.cpp")[0].range == edit.range &&
            workspace_copy.changes->at("file:///tmp/a.cpp")[0].newText == edit.newText,
        "workspace edit text changes must preserve nested edits");
    Expect(
        workspace_copy.metadata && workspace_copy.metadata->isRefactoring && *workspace_copy.metadata->isRefactoring,
        "workspace edit metadata must survive round-trip");

    lsDiagnostic diagnostic;
    diagnostic.range = lsRange(lsPosition(4, 1), lsPosition(4, 8));
    diagnostic.severity = lsDiagnosticSeverity::Warning;
    diagnostic.source = std::string("lspcpp-test");
    diagnostic.message = "warning text";
    diagnostic.tags = std::vector<DiagnosticTag> {DiagnosticTag::Unnecessary};

    lsDiagnostic const diagnostic_copy = RoundTrip(diagnostic);
    Expect(diagnostic_copy.range == diagnostic.range, "diagnostic range must round-trip");
    Expect(
        diagnostic_copy.severity && *diagnostic_copy.severity == lsDiagnosticSeverity::Warning,
        "diagnostic severity must round-trip");
    Expect(diagnostic_copy.source && *diagnostic_copy.source == "lspcpp-test", "diagnostic source must round-trip");
    Expect(diagnostic_copy.message == "warning text", "diagnostic message must round-trip");
    Expect(
        diagnostic_copy.tags && diagnostic_copy.tags->size() == 1 &&
            (*diagnostic_copy.tags)[0] == DiagnosticTag::Unnecessary,
        "diagnostic tags must round-trip");
}

void TestCompletionHoverAndInitializeRoundTrip()
{
    lsCompletionItem item;
    item.label = "printf";
    item.kind = lsCompletionItemKind::Function;
    item.detail = std::string("int printf(const char*, ...)");
    item.insertText = std::string("printf($1)");
    item.insertTextFormat = lsInsertTextFormat::Snippet;
    item.commitCharacters = std::vector<std::string> {"(", "."};

    lsCompletionItem const item_copy = RoundTrip(item);
    Expect(item_copy.label == "printf", "completion item label must round-trip");
    Expect(item_copy.kind && *item_copy.kind == lsCompletionItemKind::Function, "completion item kind must round-trip");
    Expect(item_copy.insertText && *item_copy.insertText == "printf($1)", "completion insertText must round-trip");
    Expect(
        item_copy.commitCharacters && item_copy.commitCharacters->size() == 2,
        "completion commit characters must round-trip");

    TextDocumentHover::Result hover;
    MarkupContent markdown;
    markdown.kind = "markdown";
    markdown.value = "**symbol**";
    hover.contents.second = markdown;
    hover.range = lsRange(lsPosition(2, 0), lsPosition(2, 6));
    TextDocumentHover::Result const hover_copy = RoundTrip(hover);
    Expect(hover_copy.contents.second.has_value(), "hover markup content must round-trip");
    Expect(
        hover_copy.contents.second && hover_copy.contents.second->value == "**symbol**",
        "hover markup value must round-trip");
    Expect(hover_copy.range && *hover_copy.range == *hover.range, "hover range must round-trip");

    td_initialize::request initialize_request;
    initialize_request.id.set("init-1");
    initialize_request.params.processId = 1234;
    initialize_request.params.clientInfo = ClientInfo();
    initialize_request.params.clientInfo->name = "roundtrip-client";
    initialize_request.params.rootUri = MakeDocumentUri("file:///tmp/project");
    td_initialize::request const request_copy = RoundTrip(initialize_request);
    Expect(request_copy.id == initialize_request.id, "initialize request id must round-trip");
    Expect(request_copy.method == "initialize", "initialize request method must round-trip");
    Expect(
        request_copy.params.clientInfo && request_copy.params.clientInfo->name == "roundtrip-client",
        "initialize request clientInfo must round-trip");

    td_initialize::response initialize_response;
    initialize_response.id.set("init-1");
    initialize_response.result.capabilities.hoverProvider = true;
    td_initialize::response const response_copy = RoundTrip(initialize_response);
    Expect(response_copy.id == initialize_response.id, "initialize response id must round-trip");
    Expect(
        response_copy.result.capabilities.hoverProvider &&
            *response_copy.result.capabilities.hoverProvider == true,
        "initialize response capabilities must round-trip");
}

void TestDocumentUriEscapingRoundTrip()
{
    AbsolutePath path("/tmp/lspcpp path/with#symbols(a).cpp", false);
    lsDocumentUri uri(path);

    Expect(uri.raw_uri_.find("%20") != std::string::npos, "document URI must escape spaces");
    Expect(uri.raw_uri_.find("%23") != std::string::npos, "document URI must escape reserved characters");
    Expect(uri.GetRawPath() == path.path, "document URI raw path must decode escaped characters");
    Expect(uri.GetAbsolutePath().path == path.path, "document URI absolute path must decode back to original path");

    lsDocumentUri const uri_copy = RoundTrip(uri);
    Expect(uri_copy == uri, "document URI must round-trip its raw URI");
}

void TestOptionalFieldsAreOmittedWhenUnsetAndReadFromNull()
{
    lsCompletionItem item;
    item.label = "plain";
    std::string const json = SerializeJson(item);
    Expect(json.find("detail") == std::string::npos, "unset optional fields must be omitted from JSON objects");
    Expect(json.find("insertText") == std::string::npos, "unset insertText must be omitted from JSON objects");

    rapidjson::Document document;
    JsonReader reader = MakeReader(document, "null");
    optional<std::string> value = std::string("present");
    Reflect(reader, value);
    Expect(value && *value == "present", "reading null into an optional must preserve current nullopt compatibility");
}

void TestProtocolJsonHandlerRegistersCoreRequestsAndNotifications()
{
    lsp::ProtocolJsonHandler handler;

    ExpectParsesRequest(
        handler,
        td_completion::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":1,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///a.cpp"},"position":{"line":1,"character":2},"context":{"triggerKind":1}}})",
        "ProtocolJsonHandler must parse textDocument/completion requests");
    ExpectParsesRequest(
        handler,
        td_hover::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///a.cpp"},"position":{"line":1,"character":2}}})",
        "ProtocolJsonHandler must parse textDocument/hover requests");
    ExpectParsesRequest(
        handler,
        td_definition::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///a.cpp"},"position":{"line":1,"character":2}}})",
        "ProtocolJsonHandler must parse textDocument/definition requests");
    ExpectParsesRequest(
        handler,
        td_references::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":4,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///a.cpp"},"position":{"line":1,"character":2},"context":{"includeDeclaration":true}}})",
        "ProtocolJsonHandler must parse textDocument/references requests");
    ExpectParsesRequest(
        handler,
        td_rename::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":5,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///a.cpp"},"position":{"line":1,"character":2},"newName":"renamed"}})",
        "ProtocolJsonHandler must parse textDocument/rename requests");
    ExpectParsesRequest(
        handler,
        td_formatting::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":6,"method":"textDocument/formatting","params":{"textDocument":{"uri":"file:///a.cpp"},"options":{"tabSize":4,"insertSpaces":true}}})",
        "ProtocolJsonHandler must parse textDocument/formatting requests");
    ExpectParsesRequest(
        handler,
        td_codeAction::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":7,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///a.cpp"},"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}},"context":{"diagnostics":[]}}})",
        "ProtocolJsonHandler must parse textDocument/codeAction requests");
    ExpectParsesRequest(
        handler,
        td_symbol::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":8,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///a.cpp"}}})",
        "ProtocolJsonHandler must parse textDocument/documentSymbol requests");
    ExpectParsesRequest(
        handler,
        wp_symbol::request::kMethodInfo,
        R"({"jsonrpc":"2.0","id":9,"method":"workspace/symbol","params":{"query":"foo"}})",
        "ProtocolJsonHandler must parse workspace/symbol requests");

    ExpectParsesNotification(
        handler,
        Notify_TextDocumentPublishDiagnostics::notify::kMethodInfo,
        R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":"file:///a.cpp","diagnostics":[]}})",
        "ProtocolJsonHandler must parse publishDiagnostics notifications");
    ExpectParsesNotification(
        handler,
        Notify_TextDocumentDidOpen::notify::kMethodInfo,
        R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///a.cpp","languageId":"cpp","version":1,"text":"int main(){}"}}})",
        "ProtocolJsonHandler must parse didOpen notifications");
    ExpectParsesNotification(
        handler,
        Notify_TextDocumentDidChange::notify::kMethodInfo,
        R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///a.cpp","version":2},"contentChanges":[{"text":"int main(){return 0;}"}]}})",
        "ProtocolJsonHandler must parse didChange notifications");
    ExpectParsesNotification(
        handler,
        Notify_TextDocumentDidClose::notify::kMethodInfo,
        R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file:///a.cpp"}}})",
        "ProtocolJsonHandler must parse didClose notifications");
}

} // namespace

int main()
{
    TestPositionRangeLocationAndTextEditRoundTrip();
    TestWorkspaceEditAndDiagnosticRoundTrip();
    TestCompletionHoverAndInitializeRoundTrip();
    TestDocumentUriEscapingRoundTrip();
    TestOptionalFieldsAreOmittedWhenUnsetAndReadFromNull();
    TestProtocolJsonHandlerRegistersCoreRequestsAndNotifications();
    return test::Failures() == 0 ? 0 : 1;
}
