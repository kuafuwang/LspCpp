#include "LibLsp/JsonRpc/json.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/AbsolutePath.h"
#include "LibLsp/lsp/lsDocumentUri.h"
#include "LibLsp/lsp/utils.h"
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

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
using test::Expect;

#ifndef _WIN32
struct ScopedTempDir
{
    ScopedTempDir()
    {
        char path_template[] = "/tmp/lspcpp-absolute-path-XXXXXX";
        char* created = mkdtemp(path_template);
        if (created != nullptr)
        {
            path = created;
        }
    }

    ~ScopedTempDir()
    {
        if (path.empty())
        {
            return;
        }

        std::remove((path + "/link/child.cpp").c_str());
        unlink((path + "/link").c_str());
        std::remove((path + "/real/child.cpp").c_str());
        rmdir((path + "/real").c_str());
        std::remove((path + "/dir/file.cpp").c_str());
        rmdir((path + "/dir").c_str());
        rmdir(path.c_str());
    }

    std::string path;
};
#endif

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
    AbsolutePath path("/tmp/lspcpp space/with#symbols(a).cpp");
    lsDocumentUri uri(path);

    Expect(uri.raw_uri_.find("%20") != std::string::npos, "document URI must escape spaces");
    Expect(uri.raw_uri_.find("%23") != std::string::npos, "document URI must escape reserved characters");
    Expect(uri.GetRawPath() == path.path(), "document URI raw path must decode escaped characters");
    Expect(uri.GetAbsolutePath().path() == path.path(), "document URI absolute path must decode back to original path");

    lsDocumentUri const uri_copy = RoundTrip(uri);
    Expect(uri_copy == uri, "document URI must round-trip its raw URI");
}

void TestDocumentUriFromPathAndSimpleFileUri()
{
    AbsolutePath path("/tmp/simple.cpp");
    lsDocumentUri from_path = lsDocumentUri::FromPath(path);
    lsDocumentUri from_ctor(path);

    Expect(from_path == from_ctor, "FromPath must produce the same URI as the path constructor");
#if defined(_WIN32)
    Expect(from_path.raw_uri_ == "file:///tmp/simple.cpp", "simple file URI must use the file scheme prefix on Windows");
#else
    Expect(from_path.raw_uri_ == "file:///tmp/simple.cpp", "simple file URI must use the file scheme prefix on Unix");
#endif
    Expect(from_path.GetRawPath() == path.path(), "simple file URI must decode to the original path");
    Expect(from_path.GetAbsolutePath().path() == path.path(), "simple file URI absolute path must match the source path");
    Expect(
        make_file_scheme_uri(path.path()) == from_path.raw_uri_,
        "make_file_scheme_uri must use the same encoding as lsDocumentUri::SetPath");
}

void TestDocumentUriReservedCharacterEscaping()
{
    AbsolutePath path("/tmp/a b#$&()+,;?@x.cpp");
    lsDocumentUri uri(path);

    Expect(uri.raw_uri_.find("%20") != std::string::npos, "document URI must escape spaces");
    Expect(uri.raw_uri_.find("%23") != std::string::npos, "document URI must escape #");
    Expect(uri.raw_uri_.find("%24") != std::string::npos, "document URI must escape $");
    Expect(uri.raw_uri_.find("%26") != std::string::npos, "document URI must escape &");
    Expect(uri.raw_uri_.find("%28") != std::string::npos, "document URI must escape (");
    Expect(uri.raw_uri_.find("%29") != std::string::npos, "document URI must escape )");
    Expect(uri.raw_uri_.find("%2B") != std::string::npos, "document URI must escape +");
    Expect(uri.raw_uri_.find("%2C") != std::string::npos, "document URI must escape ,");
    Expect(uri.raw_uri_.find("%3B") != std::string::npos, "document URI must escape ;");
    Expect(uri.raw_uri_.find("%3F") != std::string::npos, "document URI must escape ?");
    Expect(uri.raw_uri_.find("%40") != std::string::npos, "document URI must escape @");
    Expect(uri.GetRawPath() == path.path(), "document URI must decode every escaped reserved character");
}

void TestDocumentUriPercentAndUtf8Escaping()
{
    AbsolutePath percent_path("/tmp/100%done.cpp");
    lsDocumentUri percent_uri(percent_path);
    Expect(
        percent_uri.raw_uri_.find("%25") != std::string::npos,
        "document URI must escape literal percent signs");
    Expect(
        percent_uri.GetRawPath() == percent_path.path(),
        "document URI must decode escaped percent signs back to the original path");

    AbsolutePath utf8_path("/tmp/\xE4\xB8\xAD.cpp");
    lsDocumentUri utf8_uri(utf8_path);
    Expect(
        utf8_uri.raw_uri_.find("%E4%B8%AD") != std::string::npos,
        "document URI must percent-encode UTF-8 path bytes");
    Expect(utf8_uri.GetRawPath() == utf8_path.path(), "document URI must decode UTF-8 bytes back to the path");
}

void TestDocumentUriFileLocalhostAndInvalidPercentDecoding()
{
    lsDocumentUri localhost_uri;
    localhost_uri.raw_uri_ = "file://localhost/tmp/from-client.cpp";
    Expect(
        localhost_uri.GetRawPath() == "/tmp/from-client.cpp",
        "file://localhost URIs must decode to local absolute paths");
    Expect(
        localhost_uri.GetAbsolutePath().path() == "/tmp/from-client.cpp",
        "file://localhost URIs must normalize as local file paths");

    lsDocumentUri uppercase_localhost_uri;
    uppercase_localhost_uri.raw_uri_ = "FILE://LOCALHOST/tmp/upper-client.cpp";
    Expect(
        uppercase_localhost_uri.GetRawPath() == "/tmp/upper-client.cpp",
        "file scheme and localhost authority matching must be case-insensitive");

    lsDocumentUri invalid_percent_uri;
    invalid_percent_uri.raw_uri_ = "file:///tmp/bad%ZZ.cpp";
    Expect(
        invalid_percent_uri.GetRawPath() == "/tmp/bad%ZZ.cpp",
        "invalid percent escapes must be preserved instead of corrupting the path");
}

void TestDocumentUriRelativePathAndQueryFragmentGuards()
{
    std::string const relative_uri = make_file_scheme_uri("relative/path.cpp");
    Expect(relative_uri == "file:///relative/path.cpp", "relative paths must not become URI authorities");

    lsDocumentUri relative_document_uri(AbsolutePath("relative/path.cpp"));
    Expect(
        relative_document_uri.raw_uri_.empty(),
        "lsDocumentUri must not encode invalid relative AbsolutePath values");
    Expect(
        relative_document_uri.GetAbsolutePath().empty(),
        "invalid relative document URIs must not produce an AbsolutePath");

    lsDocumentUri query_uri;
    query_uri.raw_uri_ = "file:///tmp/name%3Finside.cpp?query=value#fragment";
    Expect(
        query_uri.GetRawPath() == "/tmp/name?inside.cpp",
        "file URI query and fragment must not become part of the decoded file path");

    lsDocumentUri fragment_uri;
    fragment_uri.raw_uri_ = "file:///tmp/hash%23inside.cpp#fragment";
    Expect(
        fragment_uri.GetRawPath() == "/tmp/hash#inside.cpp",
        "file URI fragment must be stripped while encoded path hashes still decode");
}

void TestAbsolutePathValidationNormalizesRelativePaths()
{
    AbsolutePath raw("relative/../guarded/missing.cpp");
    Expect(raw.empty(), "AbsolutePath construction must reject relative path strings");
    Expect(!raw.valid(), "AbsolutePath construction must keep relative paths invalid");

    AbsolutePath normalized("relative/../guarded/missing.cpp");
    Expect(!normalized.valid(), "default validation must not qualify relative paths implicitly");

    AbsolutePath normalized_explicitly = lsp::NormalizePath("relative/../guarded/missing.cpp", false);
    Expect(normalized_explicitly.valid(), "NormalizePath must qualify relative paths explicitly");
    Expect(lsp::IsAbsolutePath(normalized_explicitly.path()), "NormalizePath must return absolute paths");
    Expect(
        lsp::EndsWith(normalized_explicitly.path(), "/guarded/missing.cpp"),
        "NormalizePath must collapse dot-dot components even when the file is missing");

    rapidjson::Document document;
    JsonReader reader = MakeReader(document, R"("relative/from-json.cpp")");
    AbsolutePath from_json;
    Reflect(reader, from_json);
    Expect(!from_json.valid(), "AbsolutePath JSON reads must reject relative path strings");
}

void TestAbsolutePathQualifyConsistency()
{
    AbsolutePath empty;
    Expect(empty.empty(), "default AbsolutePath must start with an empty path");
    Expect(!empty.valid(), "default AbsolutePath must not mark the empty path as valid");

    AbsolutePath relative("relative/path.cpp");
    Expect(!relative.valid(), "relative paths must remain invalid");

    AbsolutePath absolute("/tmp/absolute-path.cpp");
    Expect(absolute.valid(), "absolute paths must remain valid");

    AbsolutePath failed = lsp::NormalizePath("", false);
    Expect(failed.empty(), "failed NormalizePath results must keep an empty path");
    Expect(!failed.valid(), "failed NormalizePath results must not be valid");
}

void TestAbsolutePathOperatorsAndConversion()
{
    AbsolutePath a("/tmp/a.cpp");
    AbsolutePath same("/tmp/a.cpp");
    AbsolutePath b("/tmp/b.cpp");

    Expect(a == same, "AbsolutePath equality must compare path strings");
    Expect(a != b, "AbsolutePath inequality must compare path strings");
    Expect(a < b, "AbsolutePath ordering must compare path strings");
    Expect(b > a, "AbsolutePath greater-than must compare path strings");
    Expect(a.path() == "/tmp/a.cpp", "AbsolutePath path accessor must return path");

    std::ostringstream out;
    out << a;
    Expect(out.str() == "/tmp/a.cpp", "AbsolutePath stream output must write path");
}

void TestNormalizePathAllowsMissingPathsWithoutStatState()
{
    AbsolutePath normalized = lsp::NormalizePath("missing-root/child/../leaf.cpp", false);
    Expect(lsp::IsAbsolutePath(normalized.path()), "NormalizePath must qualify missing relative paths");
    Expect(
        lsp::EndsWith(normalized.path(), "/missing-root/leaf.cpp"),
        "NormalizePath must normalize missing path components without requiring stat data");

    AbsolutePath empty = lsp::NormalizePath("", false);
    Expect(empty.empty(), "NormalizePath must reject empty paths instead of manufacturing a path");
    Expect(!empty.valid(), "NormalizePath empty results must not be valid");
}

void TestAbsolutePathValidatePreservesAbsoluteInput()
{
    AbsolutePath preserved("/tmp/a/../b.cpp");
    Expect(preserved.path() == "/tmp/b.cpp", "default validation must normalize already absolute inputs");
    Expect(preserved.valid(), "default validation must keep already absolute inputs valid");
}

#ifndef _WIN32
void TestNormalizePathCollapsesAbsoluteComponents()
{
    AbsolutePath root = lsp::NormalizePath("/", false);
    Expect(root.path() == "/", "NormalizePath must preserve the filesystem root");
    Expect(root.valid(), "normalized filesystem root must be valid");

    AbsolutePath collapsed = lsp::NormalizePath("/tmp//a/./b/../c/", false);
    Expect(collapsed.path() == "/tmp/a/c", "NormalizePath must collapse slashes, dots, dot-dots, and trailing slash");

    AbsolutePath above_root = lsp::NormalizePath("/../../", false);
    Expect(above_root.path() == "/", "NormalizePath dot-dots above root must remain at root");
}

void TestNormalizePathEnsureExists()
{
    ScopedTempDir temp;
    Expect(!temp.path.empty(), "test must create a temporary directory");
    if (temp.path.empty())
    {
        return;
    }

    std::string const dir = temp.path + "/dir";
    std::string const real = temp.path + "/real";
    Expect(mkdir(dir.c_str(), 0700) == 0, "test must create a directory");
    Expect(mkdir(real.c_str(), 0700) == 0, "test must create a real symlink target directory");

    std::string const file = dir + "/file.cpp";
    std::ofstream(file.c_str()) << "int main() {}\n";
    std::string const real_child = real + "/child.cpp";
    std::ofstream(real_child.c_str()) << "int child() {}\n";

    AbsolutePath existing = lsp::NormalizePath(dir + "/../dir/file.cpp", true);
    Expect(existing.path() == file, "NormalizePath ensure_exists must accept existing paths and collapse components");

    AbsolutePath missing = lsp::NormalizePath(dir + "/missing.cpp", true);
    Expect(missing.empty(), "NormalizePath ensure_exists must reject missing paths");
    Expect(!missing.valid(), "NormalizePath missing results must not be valid");

    AbsolutePath file_as_dir = lsp::NormalizePath(file + "/child.cpp", true);
    Expect(file_as_dir.empty(), "NormalizePath ensure_exists must reject file components used as directories");

    std::string const link = temp.path + "/link";
    if (symlink("real", link.c_str()) == 0)
    {
        AbsolutePath through_link = lsp::NormalizePath(link + "/child.cpp", true);
        Expect(
            through_link.path() == link + "/child.cpp",
            "NormalizePath must validate through symlinks without expanding the symlink path");
    }
}

void TestDocumentUriDotDotNormalization()
{
    lsDocumentUri uri;
    uri.raw_uri_ = "file:///tmp/a/../b.cpp";
    Expect(uri.GetAbsolutePath().path() == "/tmp/b.cpp", "file URI GetAbsolutePath must normalize dot-dot components");
}
#endif

void TestAbsolutePathRecognitionGuards()
{
    Expect(lsp::IsUnixAbsolutePath("/"), "Unix root must be recognized as absolute");
    Expect(lsp::IsWindowsAbsolutePath("C:/"), "Windows drive roots with forward slashes must be absolute");
    Expect(lsp::IsWindowsAbsolutePath("C:\\"), "Windows drive roots with backslashes must be absolute");
    Expect(lsp::IsWindowsAbsolutePath("\\\\server\\share"), "Windows UNC paths must be absolute");
    Expect(!lsp::IsWindowsAbsolutePath("C:relative.cpp"), "drive-relative Windows paths must not be absolute");
}

#if defined(_WIN32)
void TestDocumentUriWindowsDriveLetterEscaping()
{
    AbsolutePath path("C:/Users/test/main.cpp");
    lsDocumentUri uri(path);

    Expect(uri.raw_uri_.find("C%3A") != std::string::npos, "Windows drive letters must escape the colon");
    Expect(uri.GetRawPath() == path.path(), "Windows drive letter URI must decode back to the original path");
}
#endif

void TestDocumentUriDefaultCopyEqualityAndSwap()
{
    lsDocumentUri empty;
    Expect(empty.raw_uri_.empty(), "default document URI must start empty");
    Expect(empty == lsDocumentUri(), "default document URIs must compare equal");

    AbsolutePath path("/tmp/equality.cpp");
    lsDocumentUri uri(path);
    lsDocumentUri copied(uri);

    Expect(copied == uri, "copy constructor must preserve document URI");
    Expect(uri == uri.raw_uri_, "document URI must compare equal to its raw string form");
    Expect(!(uri == "file:///other.cpp"), "document URI string comparison must reject different URIs");

    lsDocumentUri other;
    other.swap(uri);
    Expect(other == copied, "swap must move the encoded URI to the other object");
    Expect(uri.raw_uri_.empty(), "swap must leave the previous owner empty");
}

void TestDocumentUriNonFileSchemePassthrough()
{
    lsDocumentUri untitled;
    untitled.raw_uri_ = "untitled:Untitled-1";

    Expect(
        untitled.GetRawPath() == "untitled:Untitled-1",
        "non-file URI GetRawPath must return the raw URI unchanged");
    Expect(
        !untitled.GetAbsolutePath().valid(),
        "non-file URI GetAbsolutePath must not manufacture an AbsolutePath");

    lsDocumentUri const untitled_copy = RoundTrip(untitled);
    Expect(untitled_copy == untitled, "non-file document URI must round-trip through JSON");
}

void TestDocumentUriJsonDeserialization()
{
    rapidjson::Document document;
    JsonReader reader = MakeReader(document, R"("file:///client/sent.cpp")");
    lsDocumentUri from_client;
    Reflect(reader, from_client);

    Expect(
        from_client.raw_uri_ == "file:///client/sent.cpp",
        "JSON deserialization must populate raw_uri_ from client-provided strings");
    Expect(
        from_client == std::string("file:///client/sent.cpp"),
        "deserialized document URI must compare equal to the source JSON string");
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
    TestDocumentUriFromPathAndSimpleFileUri();
    TestDocumentUriReservedCharacterEscaping();
    TestDocumentUriPercentAndUtf8Escaping();
    TestDocumentUriFileLocalhostAndInvalidPercentDecoding();
    TestDocumentUriRelativePathAndQueryFragmentGuards();
    TestAbsolutePathValidationNormalizesRelativePaths();
    TestAbsolutePathQualifyConsistency();
    TestAbsolutePathOperatorsAndConversion();
    TestNormalizePathAllowsMissingPathsWithoutStatState();
    TestAbsolutePathValidatePreservesAbsoluteInput();
#ifndef _WIN32
    TestNormalizePathCollapsesAbsoluteComponents();
    TestNormalizePathEnsureExists();
    TestDocumentUriDotDotNormalization();
#endif
    TestAbsolutePathRecognitionGuards();
#if defined(_WIN32)
    TestDocumentUriWindowsDriveLetterEscaping();
#endif
    TestDocumentUriDefaultCopyEqualityAndSwap();
    TestDocumentUriNonFileSchemePassthrough();
    TestDocumentUriJsonDeserialization();
    TestOptionalFieldsAreOmittedWhenUnsetAndReadFromNull();
    TestProtocolJsonHandlerRegistersCoreRequestsAndNotifications();
    return test::Failures() == 0 ? 0 : 1;
}
