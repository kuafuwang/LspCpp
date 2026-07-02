#include "LibLsp/JsonRpc/StreamMessageProducer.h"
#include "test_helpers.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace
{
using test::CollectingIssueHandler;
using test::Expect;
using test::MakeLspFrame;
using test::StringIStream;

#ifndef _WIN32
class PosixPipe
{
public:
    PosixPipe()
    {
        Expect(::pipe(fds_) == 0, "stream producer stress pipe must open");
    }

    ~PosixPipe()
    {
        if (fds_[0] >= 0)
        {
            ::close(fds_[0]);
        }
        if (fds_[1] >= 0)
        {
            ::close(fds_[1]);
        }
    }

    int readFd() const
    {
        return fds_[0];
    }

    void writeAll(char const* data, size_t size)
    {
        size_t offset = 0;
        while (offset < size)
        {
            ssize_t const written = ::write(fds_[1], data + offset, size - offset);
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            Expect(written > 0, "stream producer stress pipe write must succeed");
            if (written <= 0)
            {
                return;
            }
            offset += static_cast<size_t>(written);
        }
    }

    void closeWrite()
    {
        if (fds_[1] >= 0)
        {
            ::close(fds_[1]);
            fds_[1] = -1;
        }
    }

private:
    int fds_[2] = {-1, -1};
};
#endif

class ChunkedIStream : public lsp::istream
{
public:
    ChunkedIStream(std::string data, size_t chunk_size) : data_(std::move(data)), chunk_size_(chunk_size)
    {
    }

    int get() override
    {
        if (pos_ >= data_.size())
        {
            eof_ = true;
            return EOF;
        }
        return static_cast<unsigned char>(data_[pos_++]);
    }

    lsp::istream& read(char* str, std::streamsize count) override
    {
        ++read_calls_;
        read_bytes_requested_ += static_cast<size_t>(count);
        for (std::streamsize i = 0; i < count; ++i)
        {
            int const c = get();
            if (c == EOF)
            {
                break;
            }
            str[i] = static_cast<char>(c);
        }
        return *this;
    }

    std::streamsize read_some(char* str, std::streamsize count) override
    {
        ++read_some_calls_;
        if (pos_ >= data_.size())
        {
            eof_ = true;
            return 0;
        }
        auto const remaining = data_.size() - pos_;
        auto const n = std::min<size_t>({remaining, static_cast<size_t>(count), chunk_size_});
        std::copy(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                  data_.begin() + static_cast<std::ptrdiff_t>(pos_ + n),
                  str);
        pos_ += n;
        return static_cast<std::streamsize>(n);
    }

    size_t readCalls() const
    {
        return read_calls_;
    }

    size_t readBytesRequested() const
    {
        return read_bytes_requested_;
    }

    size_t readSomeCalls() const
    {
        return read_some_calls_;
    }

    bool fail() override
    {
        return false;
    }

    bool bad() override
    {
        return false;
    }

    bool eof() override
    {
        return eof_;
    }

    bool good() override
    {
        return !eof_;
    }

    void clear() override
    {
        eof_ = false;
    }

    std::string what() override
    {
        return {};
    }

private:
    std::string data_;
    size_t pos_ = 0;
    size_t chunk_size_ = 0;
    size_t read_calls_ = 0;
    size_t read_bytes_requested_ = 0;
    size_t read_some_calls_ = 0;
    bool eof_ = false;
};

// Simulates legacy streams that only override get()/read(); counters prove
// large message bodies still use the bulk read() path.
class InstrumentedReadOnlyIStream : public lsp::istream
{
public:
    explicit InstrumentedReadOnlyIStream(std::string data) : data_(std::move(data))
    {
    }

    int get() override
    {
        ++get_calls_;
        if (pos_ >= data_.size())
        {
            eof_ = true;
            return EOF;
        }
        return static_cast<unsigned char>(data_[pos_++]);
    }

    lsp::istream& read(char* str, std::streamsize count) override
    {
        ++read_calls_;
        read_bytes_requested_ += static_cast<size_t>(count);
        for (std::streamsize i = 0; i < count; ++i)
        {
            int const c = get();
            if (c == EOF)
            {
                break;
            }
            str[i] = static_cast<char>(c);
        }
        return *this;
    }

    bool fail() override
    {
        return false;
    }

    bool bad() override
    {
        return false;
    }

    bool eof() override
    {
        return eof_;
    }

    bool good() override
    {
        return !eof_;
    }

    void clear() override
    {
        eof_ = false;
    }

    std::string what() override
    {
        return {};
    }

    size_t readCalls() const
    {
        return read_calls_;
    }

    size_t readBytesRequested() const
    {
        return read_bytes_requested_;
    }

private:
    std::string data_;
    size_t pos_ = 0;
    size_t get_calls_ = 0;
    size_t read_calls_ = 0;
    size_t read_bytes_requested_ = 0;
    bool eof_ = false;
};

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

void TestChunkedReadSomeDeliversMultipleBufferedFrames()
{
    // Verifies the fast buffered path: multiple small frames should be
    // delivered from read_some() without falling back to body read().
    std::string const body1 = R"({"jsonrpc":"2.0","method":"one"})";
    std::string const body2 = R"({"jsonrpc":"2.0","method":"two"})";
    auto input = std::make_shared<ChunkedIStream>(MakeLspFrame(body1) + MakeLspFrame(body2), 4096);
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 2, "buffered parser must deliver multiple frames from one input chunk");
    if (messages.size() == 2)
    {
        Expect(messages[0] == body1, "first buffered frame body mismatch");
        Expect(messages[1] == body2, "second buffered frame body mismatch");
    }
    Expect(input->readCalls() == 0, "fully buffered small frames must not fall back to body read()");
    Expect(input->readSomeCalls() == 2, "chunked stream should read all small frames in one chunk plus EOF check");
}

void TestBulkBodyReadFallbackStreamDeliversLargeBody()
{
    // Guards legacy custom streams: headers may use byte-at-a-time read_some(),
    // but large bodies must still be read correctly through bulk read().
    std::string large_body = R"({"jsonrpc":"2.0","method":"big","params":")";
    large_body.append(9000, 'x');
    large_body += "\"}";

    std::string const trailer = R"({"jsonrpc":"2.0","method":"after"})";
    auto input = std::make_shared<StringIStream>(MakeLspFrame(large_body) + MakeLspFrame(trailer));
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 2, "byte-at-a-time stream must deliver large body then trailing frame");
    if (messages.size() == 2)
    {
        Expect(messages[0] == large_body, "large body read via bulk read() must match exactly");
        Expect(messages[1] == trailer, "frame after large bulk-read body must be intact");
    }
}

void TestFallbackStreamUsesSingleBulkReadForLargeBody()
{
    // Regression guard for performance: a large body on a legacy stream must
    // request the remaining Content-Length bytes with exactly one read() call.
    std::string large_body = R"({"jsonrpc":"2.0","method":"big","params":")";
    large_body.append(9000, 'x');
    large_body += "\"}";

    auto input = std::make_shared<InstrumentedReadOnlyIStream>(MakeLspFrame(large_body));
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 1, "instrumented fallback stream must deliver one large message");
    if (!messages.empty())
    {
        Expect(messages[0] == large_body, "instrumented fallback stream large body mismatch");
    }
    Expect(input->readCalls() == 1, "large fallback body must be read with one bulk read() call");
    Expect(
        input->readBytesRequested() == large_body.size(),
        "bulk read() must request exactly the remaining Content-Length body bytes");
}

#ifndef _WIN32
void TestStdinIStreamFragmentedFramesStressProducer()
{
    PosixPipe pipe;
    auto input = std::make_shared<lsp::stdin_istream>(pipe.readFd());

    int const frame_count = 512;
    std::vector<std::string> expected;
    expected.reserve(frame_count);
    std::vector<std::string> frames;
    frames.reserve(frame_count);
    size_t const boundary_payload_sizes[] = {0, 1, 31, 127, 512, 4095, 4096, 8192};
    std::mt19937 payload_rng(0x5EED1234u);
    std::uniform_int_distribution<size_t> payload_size_dist(0, 9999);

    for (int i = 0; i < frame_count; ++i)
    {
        size_t payload_size = payload_size_dist(payload_rng);
        if ((i % 17) < 8)
        {
            payload_size = boundary_payload_sizes[static_cast<size_t>(i % 17)];
        }
        std::string body = std::string(R"({"jsonrpc":"2.0","method":"stress","seq":)") + std::to_string(i) +
            R"(,"payload":")";
        body.append(payload_size, static_cast<char>('a' + (i % 26)));
        body += "\"}";
        expected.push_back(body);
        frames.push_back(MakeLspFrame(body));
    }

    std::thread writer(
        [&]()
        {
            size_t const boundary_chunk_sizes[] = {1, 2, 3, 5, 13, 127, 4096};
            std::mt19937 chunk_rng(0xFACEB00Cu);
            std::uniform_int_distribution<size_t> chunk_size_dist(1, 4096);
            for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index)
            {
                std::string const& frame = frames[frame_index];
                size_t offset = 0;
                for (size_t chunk_index = frame_index; offset < frame.size(); ++chunk_index)
                {
                    size_t chunk = chunk_size_dist(chunk_rng);
                    if ((chunk_index % 11) < 7)
                    {
                        chunk = boundary_chunk_sizes[chunk_index % 11];
                    }
                    chunk = std::min(chunk, frame.size() - offset);
                    pipe.writeAll(frame.data() + offset, chunk);
                    offset += chunk;
                }
            }
            pipe.closeWrite();
        });

    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);
    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });
    writer.join();

    Expect(messages.size() == expected.size(), "stdin istream stress producer must deliver every fragmented frame");
    size_t const compared = std::min(messages.size(), expected.size());
    for (size_t i = 0; i < compared; ++i)
    {
        if (messages[i] != expected[i])
        {
            Expect(false, "stdin istream stress producer frame body mismatch");
            break;
        }
    }
    Expect(issues.issues.empty(), "stdin istream stress producer must not report parser issues");
}
#endif

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

void TestInvalidContentLengthReportsWarning()
{
    auto input = std::make_shared<StringIStream>("Content-Length: not-a-number\r\n\r\n{}");
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.empty(), "invalid Content-Length must not invoke callback");
    Expect(!issues.issues.empty(), "invalid Content-Length must report an issue");
}

void TestMalformedContentLengthsAreRejected()
{
    std::vector<std::string> const values = {
        "-1",
        "4x",
        "999999999999999999999999999999",
    };

    for (auto const& value : values)
    {
        auto input = std::make_shared<StringIStream>("Content-Length: " + value + "\r\n\r\n{}");
        CollectingIssueHandler issues;
        LSPStreamMessageProducer producer(issues, input);

        std::vector<std::string> messages;
        producer.listen(
            [&](std::string&& content)
            {
                messages.push_back(std::move(content));
            });

        Expect(messages.empty(), "malformed Content-Length must not invoke callback");
        Expect(!issues.issues.empty(), "malformed Content-Length must report an issue");
    }
}

void TestContentLengthAllowsWhitespace()
{
    std::string const body = R"({"jsonrpc":"2.0","method":"test","params":{}})";
    auto input = std::make_shared<StringIStream>(
        "Content-Length: \t" + std::to_string(body.size()) + "  \r\n\r\n" + body);
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 1, "Content-Length parser must allow surrounding whitespace");
    if (!messages.empty())
    {
        Expect(messages[0] == body, "whitespace Content-Length body mismatch");
    }
}

void TestContentLengthZeroDeliversEmptyBody()
{
    auto input = std::make_shared<StringIStream>("Content-Length: 0\r\n\r\n");
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 1, "Content-Length zero must deliver one empty message");
    if (!messages.empty())
    {
        Expect(messages[0].empty(), "Content-Length zero must deliver empty body");
    }
}

void TestContentTypeCharsetIsParsed()
{
    auto input = std::make_shared<StringIStream>(
        "Content-Length: 2\r\nContent-Type: application/json; charset=utf-8\r\n\r\n{}");
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 1, "Content-Type charset header must not block delivery");
    if (!messages.empty())
    {
        Expect(messages[0] == "{}", "Content-Type charset body mismatch");
    }
}

void TestMultipleHeaderLinesParsedCorrectly()
{
    std::string const body = R"({"jsonrpc":"2.0","method":"headers"})";
    auto input = std::make_shared<StringIStream>(
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 1, "multiple headers must still deliver the message");
    if (!messages.empty())
    {
        Expect(messages[0] == body, "multiple headers body mismatch");
    }
}

void TestPartialHeaderThenEofExitsCleanly()
{
    auto input = std::make_shared<StringIStream>("Content-Len");
    CollectingIssueHandler issues;
    LSPStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.empty(), "partial header at EOF must not invoke callback");
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

void TestDelimitedProducerIgnoresEmptyLines()
{
    auto input = std::make_shared<StringIStream>(
        "\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"one\"}\n"
        "\n"
        "// -----\n"
        "\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"two\"}\n"
        "\n"
        "// -----\n");
    CollectingIssueHandler issues;
    DelimitedStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 2, "empty lines must be ignored in delimited input");
    if (messages.size() == 2)
    {
        Expect(messages[0] == "{\"jsonrpc\":\"2.0\",\"method\":\"one\"}", "first empty-line message mismatch");
        Expect(messages[1] == "{\"jsonrpc\":\"2.0\",\"method\":\"two\"}", "second empty-line message mismatch");
    }
}

void TestDelimitedProducerTrimsWhitespace()
{
    auto input = std::make_shared<StringIStream>(
        " \t {\"jsonrpc\":\"2.0\",\"method\":\"trimmed\"} \t \n"
        " \t // ----- \t \n");
    CollectingIssueHandler issues;
    DelimitedStreamMessageProducer producer(issues, input);

    std::vector<std::string> messages;
    producer.listen(
        [&](std::string&& content)
        {
            messages.push_back(std::move(content));
        });

    Expect(messages.size() == 1, "whitespace-trimmed delimited input must emit one message");
    if (!messages.empty())
    {
        Expect(
            messages[0] == "{\"jsonrpc\":\"2.0\",\"method\":\"trimmed\"}",
            "delimited producer must trim whitespace around JSON lines");
    }
}
} // namespace

int main()
{
    TestValidFrameDeliversBody();
    TestMultipleFramesDeliverBothBodies();
    TestChunkedReadSomeDeliversMultipleBufferedFrames();
    TestBulkBodyReadFallbackStreamDeliversLargeBody();
    TestFallbackStreamUsesSingleBulkReadForLargeBody();
#ifndef _WIN32
    TestStdinIStreamFragmentedFramesStressProducer();
#endif
    TestMissingContentLengthReportsWarning();
    TestInvalidContentLengthReportsWarning();
    TestMalformedContentLengthsAreRejected();
    TestContentLengthAllowsWhitespace();
    TestContentLengthZeroDeliversEmptyBody();
    TestContentTypeCharsetIsParsed();
    TestMultipleHeaderLinesParsedCorrectly();
    TestPartialHeaderThenEofExitsCleanly();
    TestShortBodyExitsWithoutDeliveringMessage();
    TestBadStreamExitsCleanly();
    TestDelimitedProducerDeliversDelimitedJsonBlocks();
    TestDelimitedProducerDropsUnterminatedTrailingBlock();
    TestDelimitedProducerIgnoresEmptyLines();
    TestDelimitedProducerTrimsWhitespace();

    return test::Failures() == 0 ? 0 : 1;
}
