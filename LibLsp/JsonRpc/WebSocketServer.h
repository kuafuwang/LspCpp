#pragma once

#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <boost/asio.hpp>
#include <string>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>


#include "RemoteEndPoint.h"
#include "stream.h"


namespace lsp {
    class Log;
}


namespace lsp
{

    class websocket_stream_wraper :public istream, public ostream
    {
    public:

	    websocket_stream_wraper(boost::beast::websocket::stream<boost::beast::tcp_stream>& _w);

        boost::beast::websocket::stream<boost::beast::tcp_stream>& ws_;
        std::atomic<bool> quit{};
        std::shared_ptr < MultiQueueWaiter> request_waiter;
        ThreadedQueue< char > on_request;


	    int get() override;

        bool fail()
        {
            return  bad();
        }

	    bool bad();

        bool eof()
        {
            return  bad();
        }
        bool good()
        {
            return  !bad();
        }
        websocket_stream_wraper& read(char* str, std::streamsize count)
        {
            for (auto i = 0; i < count; ++i)
            {
                str[i] = get();
            }
            return *this;
        }

	    websocket_stream_wraper& write(const std::string& c);

	    websocket_stream_wraper& write(std::streamsize _s);

	    websocket_stream_wraper& flush();
    };
	
		struct WebSocketServerData;
	
        /// The top-level class of the HTTP server.
        class WebSocketServer
        {
        public:
            WebSocketServer(const WebSocketServer&) = delete;
            WebSocketServer& operator=(const WebSocketServer&) = delete;
            ~WebSocketServer();
            /// Construct the server to listen on the specified TCP address and port, and
            /// serve up files from the given directory.
            explicit WebSocketServer(const std::string& user_agent, const std::string& address, const std::string& port,
                std::shared_ptr < MessageJsonHandler> json_handler,
                std::shared_ptr < Endpoint> localEndPoint, lsp::Log& ,uint32_t _max_workers = 2);

            /// Run the server's io_context loop.
            void run();
            void stop();

            RemoteEndPoint remote_end_point_;
        private:
            /// Perform an asynchronous accept operation.
            void do_accept();

            /// Wait for a request to stop the server.
            void do_stop();
            WebSocketServerData* d_ptr = nullptr;
         

        };

    } // namespace 

#endif // HTTP_SERVER_HPP
