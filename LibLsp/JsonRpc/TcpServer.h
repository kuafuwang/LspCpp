#pragma once

#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <boost/asio.hpp>
#include <string>

#include "RemoteEndPoint.h"


namespace lsp {
    class Log;
}


namespace lsp
{
		struct TcpServerData;
	
        /// The top-level class of the HTTP server.
        class TcpServer
        {
        public:
            TcpServer(const TcpServer&) = delete;
            TcpServer& operator=(const TcpServer&) = delete;
            ~TcpServer();
            /// Construct the server to listen on the specified TCP address and port, and
            /// serve up files from the given directory.
            explicit TcpServer(const std::string& address, const std::string& port,
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
            TcpServerData* d_ptr = nullptr;
         

        };

    } // namespace 

#endif // HTTP_SERVER_HPP
