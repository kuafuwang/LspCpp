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


        /// The top-level class of the HTTP server.
        class TcpServer
        {
        public:
            TcpServer(const TcpServer&) = delete;
            TcpServer& operator=(const TcpServer&) = delete;

            /// Construct the server to listen on the specified TCP address and port, and
            /// serve up files from the given directory.
            explicit TcpServer(const std::string& address, const std::string& port, MessageJsonHandler& , Endpoint& , lsp::Log& ,uint32_t _max_workers = 2);

            /// Run the server's io_context loop.
            void run();
            void stop();

            std::shared_ptr<RemoteEndPoint> remote_end_point_;
        private:
            /// Perform an asynchronous accept operation.
            void do_accept();

            /// Wait for a request to stop the server.
            void do_await_stop();

            /// The io_context used to perform asynchronous operations.
            boost::asio::io_context io_context_;

            /// The signal_set is used to register for process termination notifications.
            boost::asio::signal_set signals_;

            /// Acceptor used to listen for incoming connections.
            boost::asio::ip::tcp::acceptor acceptor_;


        	
            boost::asio::ip::tcp::iostream iostream_;
            MessageJsonHandler& jsonHandler;
            uint32_t max_workers;
            Endpoint& local_endpoint;
            lsp::Log& _log;

        };

    } // namespace 

#endif // HTTP_SERVER_HPP
