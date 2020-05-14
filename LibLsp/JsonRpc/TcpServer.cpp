//
// server.cpp

#include "TcpServer.h"
#include <signal.h>
#include <utility>
#include "MessageIssue.h"


    namespace lsp {

        TcpServer::TcpServer(const std::string& address, const std::string& port, 
            MessageJsonHandler& _handler, Endpoint& _endpoint, lsp::Log& log, uint32_t _max_workers)
            : /*io_context_(1),*/
            signals_(io_context_),
            acceptor_(io_context_),
            jsonHandler(_handler),
            local_endpoint(_endpoint),
    		_log(log), max_workers(_max_workers)
       

        {
            // Register to handle the signals that indicate when the TcpServer should exit.
            // It is safe to register for the same signal multiple times in a program,
            // provided all registration for the specified signal is made through Asio.
            signals_.add(SIGINT);
            signals_.add(SIGTERM);
#if defined(SIGQUIT)
            signals_.add(SIGQUIT);
#endif // defined(SIGQUIT)

            do_await_stop();

            // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
            boost::asio::ip::tcp::resolver resolver(io_context_);
            boost::asio::ip::tcp::endpoint endpoint =
                *resolver.resolve(address, port).begin();
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            try
            {
                acceptor_.bind(endpoint);
            }
            catch (boost::system::system_error & e)
            {
                std::string temp = "Socket Server  blid faild.";
                _log.log(lsp::Log::Level::INFO , temp + e.what());
                return;
            }
            acceptor_.listen();

            do_accept();
            std::string desc = "Socket TcpServer " + address + " " + port + " start.";
            _log.log(lsp::Log::Level::INFO, desc);
        }

        void TcpServer::run()
        {
            // The io_context::run() call will block until all asynchronous operations
            // have finished. While the TcpServer is running, there is always at least one
            // asynchronous operation outstanding: the asynchronous accept call waiting
            // for new incoming connections.
            io_context_.run();

        }

        void TcpServer::stop()
        {
            io_context_.stop();
            // signals_.cancel();
        }

        void TcpServer::do_accept()
        {
            acceptor_.async_accept(
                [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
                {
                    // Check whether the TcpServer was stopped by a signal before this
                    // completion handler had a chance to run.
                    if (!acceptor_.is_open())
                    {
                        return;
                    }

                    if (!ec)
                    {
                        iostream_ = boost::asio::ip::tcp::iostream(std::move(socket));
                        remote_end_point_ = std::make_shared<RemoteEndPoint>(iostream_, iostream_, jsonHandler,local_endpoint,_log, max_workers);
                        remote_end_point_->StartThread();
                    }

                   
                });
        }

        void TcpServer::do_await_stop()
        {
            signals_.async_wait(
                [this](boost::system::error_code /*ec*/, int /*signo*/)
                {
                    // The TcpServer is stopped by cancelling all outstanding asynchronous
                    // operations. Once all operations have finished the io_context::run()
                    // call will exit.
                    acceptor_.close();
                    if(remote_end_point_)
                    {
                        remote_end_point_->StopThread();
                    }
                });
        }

    } // namespace 

