#pragma once
#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
namespace lsp
{
class stream
{
public:
    virtual ~stream() = default;
    virtual bool fail() = 0;
    virtual bool bad() = 0;
    virtual bool eof() = 0;
    virtual bool good() = 0;
    virtual void clear() = 0;
    virtual std::string what() = 0;
    virtual bool need_to_clear_the_state()
    {
        return false;
    }
    virtual void interrupt()
    {
    }

    bool operator!()
    {
        return bad();
    }
};
class istream : public stream
{
public:
    virtual int get() = 0;
    virtual ~istream() = default;
    virtual istream& read(char* str, std::streamsize count) = 0;
    virtual std::streamsize read_some(char* str, std::streamsize count)
    {
        if (count <= 0)
        {
            return 0;
        }
        int const c = get();
        if (c == EOF)
        {
            return 0;
        }
        str[0] = static_cast<char>(c);
        return 1;
    }
};
template<class T>
class base_istream : public istream
{
public:
    explicit base_istream(T& _t) : _impl(_t)
    {
    }

    int get() override
    {
        return _impl.get();
    }
    bool fail() override
    {
        return _impl.fail();
    }
    bool bad() override
    {
        return _impl.bad();
    }
    bool eof() override
    {
        return _impl.eof();
    }
    bool good() override
    {
        return _impl.good();
    }
    istream& read(char* str, std::streamsize count) override
    {
        _impl.read(str, count);
        return *this;
    }

    void clear() override
    {
        _impl.clear();
    }
    T& _impl;
};
class ostream : public stream
{
public:
    virtual ~ostream() = default;

    virtual ostream& write(std::string const&) = 0;
    virtual ostream& write(std::streamsize) = 0;
    virtual ostream& flush() = 0;
};
template<class T>
class base_ostream : public ostream
{
public:
    explicit base_ostream(T& _t) : _impl(_t)
    {
    }

    bool fail() override
    {
        return _impl.fail();
    }
    bool good() override
    {
        return _impl.good();
    }
    bool bad() override
    {
        return _impl.bad();
    }
    bool eof() override
    {
        return _impl.eof();
    }

    ostream& write(std::string const& c) override
    {
        _impl << c;
        return *this;
    }

    ostream& write(std::streamsize _s) override
    {

        _impl << std::to_string(_s);
        return *this;
    }

    ostream& flush() override
    {
        _impl.flush();
        return *this;
    }

    void clear() override
    {
        _impl.clear();
    }

protected:
    T& _impl;
};

template<class T>
class base_iostream : public istream, public ostream
{
public:
    explicit base_iostream(T& _t) : _impl(_t)
    {
    }

    int get() override
    {
        return _impl.get();
    }
    bool fail() override
    {
        return _impl.fail();
    }
    bool bad() override
    {
        return _impl.bad();
    }
    bool eof() override
    {
        return _impl.eof();
    }
    bool good() override
    {
        return _impl.good();
    }
    istream& read(char* str, std::streamsize count) override
    {
        _impl.read(str, count);
        return *this;
    }
    ostream& write(std::string const& c) override
    {
        _impl << c;
        return *this;
    }

    ostream& write(std::streamsize _s) override
    {
        _impl << std::to_string(_s);
        return *this;
    }

    ostream& flush() override
    {
        _impl.flush();
        return *this;
    }

    void clear() override
    {
        _impl.clear();
    }

protected:
    T& _impl;
};

class standard_istream : public base_istream<std::istream>
{
public:
    explicit standard_istream(std::istream& stream) : base_istream<std::istream>(stream)
    {
    }

    std::string what() override
    {
        return {};
    }

    std::streamsize read_some(char* str, std::streamsize count) override
    {
        if (count <= 0)
        {
            return 0;
        }

        int const first = _impl.get();
        if (first == EOF)
        {
            return 0;
        }

        str[0] = static_cast<char>(first);
        std::streamsize total = 1;
        auto* buffer = _impl.rdbuf();
        if (buffer && total < count)
        {
            auto const available = buffer->in_avail();
            if (available > 0)
            {
                auto const to_read = std::min<std::streamsize>(available, count - total);
                total += buffer->sgetn(str + total, to_read);
            }
        }
        return total;
    }
};

class standard_ostream : public base_ostream<std::ostream>
{
public:
    explicit standard_ostream(std::ostream& stream) : base_ostream<std::ostream>(stream)
    {
    }

    std::string what() override
    {
        return {};
    }
};

inline std::shared_ptr<istream> make_istream(std::istream& stream)
{
    return std::make_shared<standard_istream>(stream);
}

inline std::shared_ptr<ostream> make_ostream(std::ostream& stream)
{
    return std::make_shared<standard_ostream>(stream);
}

inline std::shared_ptr<istream> make_stdin_stream()
{
    return make_istream(std::cin);
}

inline std::shared_ptr<ostream> make_stdout_stream()
{
    return make_ostream(std::cout);
}
} // namespace lsp
