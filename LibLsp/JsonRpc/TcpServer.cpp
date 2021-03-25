//
// server.cpp

#include "TcpServer.h"
#include <signal.h>
#include <utility>
#include "MessageIssue.h"
#include "stream.h"


namespace lsp {
	    struct TcpServerData
	    {
		    TcpServerData(
                MessageJsonHandler& _handler, Endpoint& _endpoint, lsp::Log& log, uint32_t _max_workers) :
			    acceptor_(io_context_), proxy_iostream_(nullptr),
			    jsonHandler(_handler),
			    local_endpoint(_endpoint),
			    _log(log), max_workers(_max_workers)
		    {
		    }

	    	~TcpServerData()
		    {
              
		    }
            /// The io_context used to perform asynchronous operations.
            boost::asio::io_context io_context_;

            std::shared_ptr<boost::asio::io_service::work> work;

            /// Acceptor used to listen for incoming connections.
            boost::asio::ip::tcp::acceptor acceptor_;


          
            boost::asio::ip::tcp::iostream iostream_;
            std::shared_ptr < base_iostream<boost::asio::ip::tcp::iostream>> proxy_iostream_;
            MessageJsonHandler& jsonHandler;
            uint32_t max_workers;
            Endpoint& local_endpoint;
            lsp::Log& _log;
	    };

	    TcpServer::~TcpServer()
	    {
            delete d_ptr;
	    }

        TcpServer::TcpServer(const std::string& address, const std::string& port, 
	                         MessageJsonHandler& _handler, Endpoint& _endpoint, lsp::Log& log, uint32_t _max_workers)
            : d_ptr(new TcpServerData(_handler, _endpoint, log, _max_workers))
           
        {
           
            d_ptr->work = std::make_shared<boost::asio::io_service::work>(d_ptr->io_context_);

            // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
            boost::asio::ip::tcp::resolver resolver(d_ptr->io_context_);
            boost::asio::ip::tcp::endpoint endpoint =
                *resolver.resolve(address, port).begin();
            d_ptr->acceptor_.open(endpoint.protocol());
            d_ptr->acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            try
            {
                d_ptr->acceptor_.bind(endpoint);
            }
            catch (boost::system::system_error & e)
            {
                std::string temp = "Socket Server  blid faild.";
                d_ptr->_log.log(lsp::Log::Level::INFO , temp + e.what());
                return;
            }
            d_ptr->acceptor_.listen();

            do_accept();
            std::string desc = "Socket TcpServer " + address + " " + port + " start.";
            d_ptr->_log.log(lsp::Log::Level::INFO, desc);
        }

        void TcpServer::run()
        {
            // The io_context::run() call will block until all asynchronous operations
            // have finished. While the TcpServer is running, there is always at least one
            // asynchronous operation outstanding: the asynchronous accept call waiting
            // for new incoming connections.
            d_ptr->io_context_.run();

        }

        void TcpServer::stop()
        {
            try
            {
            	if(d_ptr->work)
                    d_ptr->work.reset();

                do_stop();
            }
            catch (...)
            {
            }
        }

        void TcpServer::do_accept()
        {
            d_ptr->acceptor_.async_accept(
                [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
                {
                    // Check whether the TcpServer was stopped by a signal before this
                    // completion handler had a chance to run.
                    if (!d_ptr->acceptor_.is_open())
                    {
                        return;
                    }

                    if (!ec)
                    {
                    	if(remote_end_point_)
                    	{
                            d_ptr->iostream_.close();
                            remote_end_point_->StopThread();
                    	}
                     
                        d_ptr->iostream_ = boost::asio::ip::tcp::iostream(std::move(socket));
                    	
                        d_ptr->proxy_iostream_ = std::make_shared < base_iostream<boost::asio::ip::tcp::iostream> >(d_ptr->iostream_);
                    	
                        remote_end_point_ = std::make_shared<RemoteEndPoint>(*(d_ptr->proxy_iostream_),
                            *(d_ptr->proxy_iostream_), d_ptr->jsonHandler, d_ptr->local_endpoint, 
                            d_ptr->_log, d_ptr->max_workers);
                    	
                        remote_end_point_->StartThread();
                        do_accept();
                    }

                   
                });
        }

        void TcpServer::do_stop()
        {
            d_ptr->acceptor_.close();
            if(remote_end_point_)
            {
                remote_end_point_->StopThread();
            }
        }

    } // namespace 

