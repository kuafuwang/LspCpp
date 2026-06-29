#pragma once

#include "LibLsp/JsonRpc/MessageIssue.h"
#include "LibLsp/JsonRpc/stream.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test
{
inline int& Failures()
{
    static int failures = 0;
    return failures;
}

inline void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << message << std::endl;
        ++Failures();
    }
}

class DummyLog : public lsp::Log
{
public:
    void log(Level, std::wstring&& msg) override
    {
        std::wcerr << msg << std::endl;
    }

    void log(Level, const std::wstring& msg) override
    {
        std::wcerr << msg << std::endl;
    }

    void log(Level, std::string&& msg) override
    {
        std::cerr << msg << std::endl;
    }

    void log(Level, const std::string& msg) override
    {
        std::cerr << msg << std::endl;
    }
};

class CollectingIssueHandler : public MessageIssueHandler
{
public:
    void handle(std::vector<MessageIssue>&&) override
    {
    }

    void handle(MessageIssue&& issue) override
    {
        issues.push_back(std::move(issue));
    }

    std::vector<MessageIssue> issues;
};

class StringIStream : public lsp::istream
{
public:
    explicit StringIStream(std::string data) : data_(std::move(data))
    {
    }

    void set_bad(bool value = true)
    {
        bad_ = value;
    }

    int get() override
    {
        if (bad_)
        {
            return EOF;
        }
        if (pos_ >= data_.size())
        {
            eof_ = true;
            return EOF;
        }
        return static_cast<unsigned char>(data_[pos_++]);
    }

    lsp::istream& read(char* str, std::streamsize count) override
    {
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
        return fail_;
    }

    bool bad() override
    {
        return bad_;
    }

    bool eof() override
    {
        return eof_;
    }

    bool good() override
    {
        return !fail_ && !bad_ && !eof_;
    }

    void clear() override
    {
        fail_ = false;
        bad_ = false;
        eof_ = false;
    }

    std::string what() override
    {
        return what_;
    }

private:
    std::string data_;
    size_t pos_ = 0;
    bool fail_ = false;
    bool bad_ = false;
    bool eof_ = false;
    std::string what_;
};

class StringOStream : public lsp::ostream
{
public:
    void set_bad(bool value = true)
    {
        bad_ = value;
    }

    std::string const& data() const
    {
        return data_;
    }

    lsp::ostream& write(std::string const& value) override
    {
        if (!bad_)
        {
            data_ += value;
        }
        return *this;
    }

    lsp::ostream& write(std::streamsize value) override
    {
        return write(std::to_string(value));
    }

    lsp::ostream& flush() override
    {
        return *this;
    }

    bool fail() override
    {
        return fail_;
    }

    bool bad() override
    {
        return bad_;
    }

    bool eof() override
    {
        return false;
    }

    bool good() override
    {
        return !fail_ && !bad_;
    }

    void clear() override
    {
        fail_ = false;
        bad_ = false;
    }

    std::string what() override
    {
        return what_;
    }

private:
    std::string data_;
    bool fail_ = false;
    bool bad_ = false;
    std::string what_;
};

inline std::string MakeLspFrame(std::string const& body)
{
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

inline int ParseContentLength(std::string const& framed)
{
    auto const header_end = framed.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        return -1;
    }
    auto const colon = framed.find(':');
    if (colon == std::string::npos)
    {
        return -1;
    }
    return std::atoi(framed.substr(colon + 1, header_end - colon - 1).c_str());
}

} // namespace test
