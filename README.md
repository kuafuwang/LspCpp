# LspCpp

## Dependencies
`LspCpp` depends on the boost and rapidjson

## Build
 * `1.Open project with Vistual Studio.
 * `2.[Restore packages][3]
 * `3.Build it.
 
## Reference
 Some code from :[cquery][1]

## Projects using LspCpp:
* [JCIDE](https://www.javacardos.com/tools)
## License
   MIT
   
##  Demo:
```cpp

#include "LibLsp/lsp/general/exit.h"
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
		std::wcout << msg << std::endl;
	};
	void log(Level level, const std::wstring& msg)
	{
		std::wcout << msg << std::endl;
	};
	void log(Level level, std::string&& msg)
	{
		std::cout << msg << std::endl;
	};
	void log(Level level, const std::string& msg)
	{
		std::cout << msg << std::endl;
	};
};



class StdIOServer
{
public:
	StdIOServer() : remote_end_point_(protocol_json_handler, endpoint, _log)
	{
		remote_end_point_.registerRequestHandler([&](const td_initialize::request& req)
			{
				td_initialize::response rsp;
				rsp.id = req.id;
				CodeLensOptions code_lens_options;
				code_lens_options.resolveProvider = true;
				rsp.result.capabilities.codeLensProvider = code_lens_options;
				return rsp;
			});

		remote_end_point_.registerNotifyHandler([=](Notify_Exit::notify& notify)
			{
				std::cout << notify.ToJson() << std::endl;
			});
		remote_end_point_.startProcessingMessages(input, output);
	}
	~StdIOServer()
	{
		
	}
	
	std::shared_ptr < lsp::ProtocolJsonHandler >  protocol_json_handler = std::make_shared < lsp::ProtocolJsonHandler >();
	DummyLog _log;
	
	std::shared_ptr<lsp::base_ostream<std::ostream> > output = std::make_shared<lsp::base_ostream<std::ostream>>(std::cout);
	std::shared_ptr<lsp::base_istream<std::istream> > input = std::make_shared<lsp::base_istream<std::istream>>(std::cin);

	std::shared_ptr < GenericEndpoint >  endpoint = std::make_shared<GenericEndpoint>(_log);
	RemoteEndPoint remote_end_point_;

};

std::string _address = "127.0.0.1";
std::string _port = "9333";

class Server
{
public:


	Server():server(_address,_port,protocol_json_handler, endpoint, _log)
	{
		server.remote_end_point_.registerRequestHandler([&](const td_initialize::request& req)
			{
				td_initialize::response rsp;
				rsp.id = req.id;
				CodeLensOptions code_lens_options;
				code_lens_options.resolveProvider = true;
				rsp.result.capabilities.codeLensProvider = code_lens_options;
				return rsp;
			});
		server.remote_end_point_.registerNotifyHandler([=](Notify_Exit::notify& notify)
			{
				std::cout << notify.ToJson() << std::endl;
			});
		std::thread([&]()
			{
				server.run();
			}).detach();
	}
	~Server()
	{
		server.stop();
	}
	std::shared_ptr < lsp::ProtocolJsonHandler >  protocol_json_handler = std::make_shared < lsp::ProtocolJsonHandler >();
	DummyLog _log;

	std::shared_ptr < GenericEndpoint >  endpoint = std::make_shared<GenericEndpoint>(_log);
	lsp::TcpServer server;

};

class Client
{
public:
	Client() :remote_end_point_(protocol_json_handler, endpoint, _log)
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
	
		remote_end_point_.startProcessingMessages(socket_proxy, socket_proxy);
	}
	~Client()
	{
		remote_end_point_.Stop();
		::Sleep(1000);
		socket_->close();
	}

	std::shared_ptr < lsp::ProtocolJsonHandler >  protocol_json_handler = std::make_shared< lsp::ProtocolJsonHandler>();
	DummyLog _log;

	std::shared_ptr<GenericEndpoint>  endpoint = std::make_shared<GenericEndpoint>(_log);

	std::shared_ptr < lsp::base_iostream<std::iostream>> socket_proxy;
	std::shared_ptr<tcp::iostream>  socket_;
	RemoteEndPoint remote_end_point_;
};

int main() 
{

	Server server;
	Client client;

	Notify_Exit::notify notify;
	client.remote_end_point_.sendNotification(notify);
	
	td_initialize::request req;
	{
		auto rsp = client.remote_end_point_.waitResponse(req);
		if (rsp)
		{
			std::cout << rsp->ToJson() << std::endl;
		}
		else
		{
			std::cout << "get initialze  response time out" << std::endl;
		}
	}

	auto future_rsp = client.remote_end_point_.sendRequest(req);
	auto state = future_rsp.wait_for(std::chrono::seconds(4));
	if (std::future_status::timeout == state)
	{
		std::cout << "get initialze  response time out" << std::endl;
		return 0;
	}
	auto rsp = future_rsp.get();
	if (rsp.error)
	{
		std::cout << "get initialze  response error" << std::endl;
		
	}
	else
	{
		std::cout << rsp.response.ToJson() << std::endl;
	}
	return 0;
}
#endif

#endif



```


[1]: https://github.com/cquery-project/cquery "cquery:"
[2]: https://www.javacardos.com/tools "JcKit:"
[3]: https://docs.microsoft.com/en-us/nuget/consume-packages/package-restore "Package Restore"
