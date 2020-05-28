//
// server.cpp

#include "TcpServer.h"
#include <signal.h>
#include <utility>
#include "MessageIssue.h"


    namespace lsp {

        TcpServer::TcpServer(const std::string& address, const std::string& port, 
            MessageJsonHandler& _handler, Endpoint& _endpoint, lsp::Log& log, uint32_t _max_workers)
            : 
            acceptor_(io_context_),
            jsonHandler(_handler),
            local_endpoint(_endpoint),
    		_log(log), max_workers(_max_workers)
       

        {
           
            work = std::make_shared<boost::asio::io_service::work>(io_context_);

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
            try
            {
                work.reset();

                do_stop();
            }
            catch (...)
            {
            }
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
                    	if(remote_end_point_)
                    	{
                            iostream_.close();
                            remote_end_point_->StopThread();
                    	}
                        iostream_ = boost::asio::ip::tcp::iostream(std::move(socket));
                        remote_end_point_ = std::make_shared<RemoteEndPoint>(iostream_, iostream_, jsonHandler,local_endpoint,_log, max_workers);
                        remote_end_point_->StartThread();
                        do_accept();
                    }

                   
                });
        }

        void TcpServer::do_stop()
        {
            acceptor_.close();
            if(remote_end_point_)
            {
                remote_end_point_->StopThread();
            }
        }

    } // namespace 

