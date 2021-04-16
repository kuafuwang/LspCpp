#include "LibLsp/JsonRpc/Condition.h"
#ifdef STDIO_SERVER_EXAMPLE

#include "LibLsp/lsp/general/exit.h"
#include "LibLsp/lsp/textDocument/declaration_definition.h"
#include <boost/program_options.hpp>
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
#include <boost/process.hpp>
#include <boost/process/windows.hpp>
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

		remote_end_point_.registerNotifyHandler([&](Notify_Exit::notify& notify)
			{
				remote_end_point_.Stop();
				esc_event.notify(std::make_unique<bool>(true));
			});
		remote_end_point_.registerRequestHandler([&](const td_definition::request& req)
			{
				td_definition::response rsp;
				rsp.result.first = std::vector<lsLocation>();
				return rsp;
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
	Condition<bool> esc_event;
};




int main(int argc, char* argv[])
{
	using namespace  boost::program_options;
	options_description desc("Allowed options");
	desc.add_options()
		("help,h", "produce help message");



	variables_map vm;
	try {
		store(parse_command_line(argc, argv, desc), vm);
	}
	catch (std::exception& e) {
		std::cout << "Undefined input.Reason:" << e.what() << std::endl;
		return 0;
	}
	notify(vm);


	if (vm.count("help"))
	{
		cout << desc << endl;
		return 1;
	}
	StdIOServer server;
	server.esc_event.wait();

	return 0;
}
#endif

