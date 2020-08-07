

#include "LibLsp/lsp/textDocument/signature_help.h"
#include "LibLsp/lsp/general/initialize.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/textDocument/typeHierarchy.h"
#include "LibLsp/lsp/AbsolutePath.h"
#include "LibLsp/lsp/textDocument/resolveCompletionItem.h"
#include <network/uri.hpp>

#include "LibLsp/JsonRpc/TcpServer.h"

#ifdef _CONSOLE
#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include "sct/ApduModelTest.h"
#include <iostream>
using namespace boost::asio::ip;
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

		auto  socket_ = std::make_shared<tcp::iostream>();
		socket_->connect(end_point);
		if (!socket_)
		{
			string temp = "Unable to connect: " + socket_->error().message();
			std::cout << temp << std::endl;
		}
		write_to_service = socket_;
		read_from_service = socket_;


		vector<string> args;

	
		remote_end_point_ = std::make_shared<RemoteEndPoint>(*(read_from_service.get()), *(write_to_service.get()),
			protocol_json_handler, endpoint, _log);


		remote_end_point_->StartThread();
	}
	
	lsp::ProtocolJsonHandler  protocol_json_handler;
	DummyLog _log;
	GenericEndpoint endpoint;
	std::shared_ptr<RemoteEndPoint> remote_end_point_;
	
	std::shared_ptr<std::ostream>  write_to_service;
	std::shared_ptr< std::istream >   read_from_service;
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
