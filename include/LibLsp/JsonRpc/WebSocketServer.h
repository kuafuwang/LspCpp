#pragma once

#include <string>
#include <LibLsp/lsp/asio.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include "RemoteEndPoint.h"
#include "stream.h"
#include "threaded_queue.h"

namespace lsp
{
class Log;
}

namespace lsp
{

class websocket_stream_wrapper : public istream, public ostream
{
public:
    websocket_stream_wrapper(std::shared_ptr<ix::WebSocket> _w);

    std::shared_ptr<ix::WebSocket> ws_;
    std::atomic<bool> quit {};
    std::shared_ptr<MultiQueueWaiter> request_waiter;
    ThreadedQueue<char> on_request;
    std::string error_message;
    bool fail() override;

    bool eof() override;

    bool good() override;

    websocket_stream_wrapper& read(char* str, std::streamsize count) override;

    int get() override;

    bool bad() override;

    websocket_stream_wrapper& write(std::string const& c) override;

    websocket_stream_wrapper& write(std::streamsize _s) override;

    websocket_stream_wrapper& flush() override;

    void clear() override;

    std::string what() override;
};

/// The top-level class of the HTTP server.
class WebSocketServer
{
public:
    WebSocketServer(WebSocketServer const&) = delete;
    WebSocketServer& operator=(WebSocketServer const&) = delete;
    ~WebSocketServer();
    /// Construct the server to listen on the specified TCP address and port, and
    /// serve up files from the given directory.
    explicit WebSocketServer(
        std::string const& user_agent, std::string const& address, std::string const& port,
        std::shared_ptr<MessageJsonHandler> json_handler, std::shared_ptr<Endpoint> localEndPoint, lsp::Log&,
        uint32_t _max_workers = 2
    );

    /// Run the server's io_context loop.
    void run();
    void stop();

    RemoteEndPoint point;

private:
    struct Data;

    Data* d_ptr = nullptr;
};

} // namespace lsp
