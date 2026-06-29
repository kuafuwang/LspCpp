#include "LibLsp/JsonRpc/json.h"
#include "LibLsp/JsonRpc/lsRequestId.h"
#include "LibLsp/lsp/general/initialize.h"
#include "test_helpers.h"

#include <cstdint>
#include <map>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <string>

namespace
{
using test::Expect;

lsRequestId ParseRequestIdFromJson(char const* json)
{
    rapidjson::Document document;
    document.Parse(json);
    JsonReader reader(&document);
    lsRequestId id;
    Reflect(reader, id);
    return id;
}

std::string SerializeRequestIdToJson(lsRequestId const& id)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    JsonWriter json_writer(&writer);
    lsRequestId copy = id;
    Reflect(json_writer, copy);
    return buffer.GetString();
}

void TestRequestIdOrderingKeepsTypesDistinct()
{
    lsRequestId int_id;
    int_id.set(1);

    lsRequestId string_id;
    string_id.set("1");

    std::map<lsRequestId, std::string> ids;
    ids[int_id] = "int";
    ids[string_id] = "string";

    Expect(ids.size() == 2, "int and string request ids must be distinct map keys");
    Expect(ids[int_id] == "int", "integer request id lookup returned wrong value");
    Expect(ids[string_id] == "string", "string request id lookup returned wrong value");
}

void TestRequestIdOrderingKeepsLargeIntegersDistinct()
{
    lsRequestId small;
    small.set(1);

    lsRequestId large;
    large.type = lsRequestId::kInt;
    large.value = static_cast<int64_t>(1) << 40;

    std::map<lsRequestId, std::string> ids;
    ids[small] = "small";
    ids[large] = "large";

    Expect(ids.size() == 2, "large integer request ids must not collide");
    Expect(ids[large] == "large", "large integer request id lookup returned wrong value");
}

void TestRequestIdStringRoundTrip()
{
    lsRequestId const id = ParseRequestIdFromJson("\"abc-123\"");

    Expect(id.type == lsRequestId::kString, "string request id must stay string type");
    Expect(id.k_string == "abc-123", "string request id value mismatch");
    Expect(id.value == -1, "string request id must not derive a numeric value");
    Expect(SerializeRequestIdToJson(id) == "\"abc-123\"", "string request id must serialize as JSON string");
}

void TestNumericStringRequestIdDoesNotBecomeInteger()
{
    lsRequestId const id = ParseRequestIdFromJson("\"123\"");

    Expect(id.type == lsRequestId::kString, "numeric-looking string request id must stay string type");
    Expect(id.k_string == "123", "numeric-looking string request id text mismatch");
    Expect(id.value == -1, "numeric-looking string request id must not populate integer value");
    Expect(SerializeRequestIdToJson(id) == "\"123\"", "numeric-looking string id must serialize as JSON string");
}

void TestRequestIdNullAndInvalid()
{
    lsRequestId const null_id = ParseRequestIdFromJson("null");
    Expect(null_id.type == lsRequestId::kNone, "null request id must become kNone");
    Expect(SerializeRequestIdToJson(null_id) == "null", "kNone request id must serialize as null");

    lsRequestId const bool_id = ParseRequestIdFromJson("true");
    Expect(bool_id.type == lsRequestId::kNone, "non-int/non-string request id must become kNone");
}

void TestLspMessageWriteFraming()
{
    td_initialize::request req;
    req.id.set(42);

    std::ostringstream out;
    req.Write(out);
    std::string const framed = out.str();

    Expect(framed.find("Content-Length: ") == 0, "framed message must start with Content-Length header");

    auto const header_end = framed.find("\r\n\r\n");
    Expect(header_end != std::string::npos, "framed message must contain header/body separator");

    std::string const body = framed.substr(header_end + 4);
    int const content_length = test::ParseContentLength(framed);
    Expect(content_length >= 0, "Content-Length header must be parseable");
    Expect(static_cast<size_t>(content_length) == body.size(), "Content-Length must match JSON body size");
    Expect(body.find("\"id\":42") != std::string::npos, "framed body must contain serialized request id");
}

void TestRequestSwapSwapsMethod()
{
    td_initialize::request first;
    td_initialize::request second;
    first.SetMethodType("first");
    second.SetMethodType("second");

    first.swap(second);

    Expect(first.method == "second", "request swap must take the other method");
    Expect(second.method == "first", "request swap must give the original method to the other request");
}
} // namespace

int main()
{
    TestRequestIdOrderingKeepsTypesDistinct();
    TestRequestIdOrderingKeepsLargeIntegersDistinct();
    TestRequestIdStringRoundTrip();
    TestNumericStringRequestIdDoesNotBecomeInteger();
    TestRequestIdNullAndInvalid();
    TestLspMessageWriteFraming();
    TestRequestSwapSwapsMethod();

    return test::Failures() == 0 ? 0 : 1;
}
