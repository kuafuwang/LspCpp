#ifdef _CONSOLE
#ifdef TCP_SERVER_EXAMPLE

#include "LibLsp/lsp/textDocument/signature_help.h"
#include "LibLsp/lsp/general/initialize.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/textDocument/typeHierarchy.h"
#include "LibLsp/lsp/AbsolutePath.h"
#include "LibLsp/lsp/textDocument/resolveCompletionItem.h"
#include <network/uri.hpp>


#include "LibLsp/JsonRpc/Endpoint.h"
#include "LibLsp/JsonRpc/stream.h"
#include "LibLsp/JsonRpc/TcpServer.h"
#include "LibLsp/lsp/textDocument/document_symbol.h"
#include "LibLsp/lsp/workspace/execute_command.h"

#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include <iostream>
using namespace boost::asio::ip;
using namespace std;
class DummyLog :public lsp::Log
{
public:
	
	void log(Level level, std::wstring&& msg)
	{
		
	};
	void log(Level level, const std::wstring& msg)
	{
		
	};
	void log(Level level, std::string&& msg)
	{
		
	};
	void log(Level level, const std::string& msg)
	{
		
	};
};
std::string _address = "127.0.0.1";
std::string _port = "9333";

class Server
{
public:
	Server(): endpoint(_log),server(_address, _port, protocol_json_handler, endpoint, _log)
	{
		endpoint.method2request[td_initialize::kMethodType] = [&](std::unique_ptr<LspMessage> msg)
		{
			auto req = reinterpret_cast<td_initialize::request*>(msg.get());
			td_initialize::response rsp;
			rsp.id = req->id;
			CodeLensOptions code_lens_options;
			code_lens_options.resolveProvider = true;
			rsp.result.capabilities.codeLensProvider = code_lens_options;
			server.remote_end_point_->sendResponse(rsp);
			return true;
		};
		
		std::thread([&]()
			{
				server.run();
		}).detach();
	}
	~Server()
	{
		server.stop();
	}
	lsp::ProtocolJsonHandler  protocol_json_handler;
	DummyLog _log;
	GenericEndpoint endpoint;
	lsp::TcpServer server;
	
};

class Client
{
public:
	Client() : endpoint(_log)
	{
		tcp::endpoint end_point( address::from_string(_address), 9333);

		socket_ = std::make_shared<tcp::iostream>();
		socket_->connect(end_point);
		if (!socket_)
		{
			string temp = "Unable to connect: " + socket_->error().message();
			std::cout << temp << std::endl;
		}
		

		vector<string> args;
		socket_proxy = std::make_shared<lsp::base_iostream<std::iostream>>(*socket_.get());
	
		remote_end_point_ = std::make_shared<RemoteEndPoint>(*socket_proxy.get(), *socket_proxy.get(),
			protocol_json_handler, endpoint, _log);


		remote_end_point_->StartThread();
	}
	~Client()
	{
		remote_end_point_->StopThread();
		::Sleep(1000);
		socket_->close();
	}
	lsp::ProtocolJsonHandler  protocol_json_handler;
	DummyLog _log;
	GenericEndpoint endpoint;
	std::shared_ptr<RemoteEndPoint> remote_end_point_;
	std::shared_ptr < lsp::base_iostream<std::iostream>> socket_proxy;
	std::shared_ptr<tcp::iostream>  socket_;

};

int main() 
{
	
	Server server;
	Client client;


	
	td_initialize::request req;
	
	auto rsp = client.remote_end_point_->waitResponse(req);
	if(rsp)
	{
		std::cout << rsp->ToJson() << std::endl;
	}
	return 0;
}
#endif

#endif
