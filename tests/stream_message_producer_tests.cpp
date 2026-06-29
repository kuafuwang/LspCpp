#include "LibLsp/JsonRpc/StreamMessageProducer.h"
#include "test_helpers.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
using test::CollectingIssueHandler;
using test::Expect;
using test::MakeLspFrame;
using test::StringIStream;

void TestValidFrameDeliversBody()
{
    std::string const body = R"({"jsonrpc":"2.0","method":"test","params":{}})";
    auto input = std::make_shared<StringIStream>(MakeLspFrame(body));
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 1, "valid frame must invoke callback once");
    Expect(messages[0] == body, "valid frame must deliver exact JSON body");
}

void TestMultipleFramesDeliverBothBodies()
{
    std::string const body1 = R"({"jsonrpc":"2.0","method":"one"})";
    std::string const body2 = R"({"jsonrpc":"2.0","method":"two"})";
    auto input = std::make_shared<StringIStream>(MakeLspFrame(body1) + MakeLspFrame(body2));
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 2, "multiple frames must invoke callback twice");
    Expect(messages[0] == body1, "first frame body mismatch");
    Expect(messages[1] == body2, "second frame body mismatch");
}

void TestMissingContentLengthReportsWarning()
{
    auto input = std::make_shared<StringIStream>("Content-Type: application/json\r\n\r\n");
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.empty(), "missing Content-Length must not invoke callback");
    Expect(!issues.issues.empty(), "missing Content-Length must report an issue");
    Expect(
        issues.issues[0].text.find("Content-Length") != std::string::npos,
        "missing Content-Length issue must mention Content-Length");
}

void TestShortBodyExitsWithoutDeliveringMessage()
{
    std::string const body = R"({"jsonrpc":"2.0"})";
    std::string frame = "Content-Length: 50\r\n\r\n" + body;
    auto input = std::make_shared<StringIStream>(frame);
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.empty(), "short body must not deliver a complete message");
    Expect(!issues.issues.empty(), "short body must report an issue");
}

void TestBadStreamExitsCleanly()
{
    auto input = std::make_shared<StringIStream>("");
    input->set_bad(true);
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.empty(), "bad stream must not invoke callback");
    Expect(!issues.issues.empty(), "bad stream must report an issue");
    Expect(
        issues.issues[0].text.find("Input stream is bad") != std::string::npos,
        "bad stream issue must mention bad input");
}

void TestDelimitedProducerDeliversDelimitedJsonBlocks()
{
    auto input = std::make_shared<StringIStream>(
        "// comment ignored\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"one\"}\n"
        "// -----\n"
        "// another comment ignored\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"two\"}\n"
        "// -----\n");
    CollectingIssueHandler issues;
    DelimitedStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 2, "delimited producer must emit two messages");
    if (messages.size() == 2)
    {
        Expect(
            messages[0] == "{\"jsonrpc\":\"2.0\",\"method\":\"one\"}",
            "first delimited message body mismatch");
        Expect(
            messages[1] == "{\"jsonrpc\":\"2.0\",\"method\":\"two\"}",
            "second delimited message body mismatch");
    }
}

void TestDelimitedProducerDropsUnterminatedTrailingBlock()
{
    auto input = std::make_shared<StringIStream>(
        "{\"jsonrpc\":\"2.0\",\"method\":\"unterminated\"}\n");
    CollectingIssueHandler issues;
    DelimitedStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.empty(), "unterminated delimited block must not be emitted");
}
} // namespace

int main()
{
    TestValidFrameDeliversBody();
    TestMultipleFramesDeliverBothBodies();
    TestMissingContentLengthReportsWarning();
    TestShortBodyExitsWithoutDeliveringMessage();
    TestBadStreamExitsCleanly();
    TestDelimitedProducerDeliversDelimitedJsonBlocks();
    TestDelimitedProducerDropsUnterminatedTrailingBlock();

    return test::Failures() == 0 ? 0 : 1;
}
