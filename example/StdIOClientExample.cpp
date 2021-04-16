
#ifdef STDIO_CLIENT_EXAMPLE

#include <boost/program_options.hpp>
#include "LibLsp/lsp/general/exit.h"
#include "LibLsp/lsp/textDocument/declaration_definition.h"

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


class ProcessIoService
{
public:
	using IOService = boost::asio::io_service;
	using Work = boost::asio::io_service::work;
	using WorkPtr = std::unique_ptr<Work>;

	ProcessIoService() {

		work_ = std::unique_ptr<Work>(new Work(ioService_));
		auto temp_thread_ = new std::thread([this]
			{
				ioService_.run();
			});
		thread_ = std::unique_ptr<std::thread>(temp_thread_);
	}

	ProcessIoService(const ProcessIoService&) = delete;
	ProcessIoService& operator=(const ProcessIoService&) = delete;

	boost::asio::io_service& getIOService()
	{
		return ioService_;
	}

	void stop()
	{

		work_.reset();

		thread_->join();

	}

private:
	IOService       ioService_;
	WorkPtr        work_;
	std::unique_ptr<std::thread>    thread_;

};

class Client
{
public:
	Client(std::string& execPath,const std::vector<std::string>& args) 
		:remote_end_point_(protocol_json_handler, endpoint, _log)
	{
		std::error_code ec;
		namespace bp = boost::process;
		c = std::make_shared<bp::child>(asio_io.getIOService(), execPath, 
			bp::args = args,
			ec,
			bp::windows::show,
			bp::std_out > read_from_service,
			bp::std_in < write_to_service,
			bp::on_exit([&](int exit_code, const std::error_code& ec_in){
				
			}));
		if (ec)
		{
			// fail
			_log.log(lsp::Log::Level::SEVERE, "Start server failed.");
		}
		else {
			//success
			remote_end_point_.startProcessingMessages(read_from_service_proxy, write_to_service_proxy);
		}
	}
	~Client()
	{
		remote_end_point_.Stop();
		::Sleep(1000);
		
	}
	ProcessIoService asio_io;
	std::shared_ptr < lsp::ProtocolJsonHandler >  protocol_json_handler = std::make_shared< lsp::ProtocolJsonHandler>();
	DummyLog _log;

	std::shared_ptr<GenericEndpoint>  endpoint = std::make_shared<GenericEndpoint>(_log);

	std::shared_ptr < lsp::base_iostream<std::iostream>> socket_proxy;
	boost::process::opstream write_to_service;
	boost::process::ipstream   read_from_service;

	std::shared_ptr<lsp::ostream> write_to_service_proxy = std::make_shared<lsp::base_ostream<std::ostream>>(write_to_service);
	std::shared_ptr<lsp::istream>  read_from_service_proxy = std::make_shared< lsp::base_istream<std::istream> >(read_from_service);

	std::shared_ptr<boost::process::child> c;
	RemoteEndPoint remote_end_point_;
};

int main(int argc, char* argv[])
{
	using namespace  boost::program_options;
	options_description desc("Allowed options");
	desc.add_options()
		("help,h", "produce help message")
		("execPath", value<string>(), "LSP server's path");
	


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
	string execPath;
	if (vm.count("execPath"))
	{
		execPath = vm["execPath"].as<string>();
	}
	else
	{
		execPath = "STDIO_SERVER_EXAMPLE.exe";
	}

	Client client(execPath, {});
	{
		td_initialize::request req;
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
	{
		td_definition::request req;
		auto future_rsp = client.remote_end_point_.sendRequest(req);
		auto state = future_rsp.wait_for(std::chrono::seconds(4));
		if (std::future_status::timeout == state)
		{
			std::cout << "get textDocument/definition  response time out" << std::endl;
			return 0;
		}
		auto rsp = future_rsp.get();
		if (rsp.error)
		{
			std::cout << "get textDocument/definition  response error" << std::endl;

		}
		else
		{
			std::cout << rsp.response.ToJson() << std::endl;
		}
	}
	Notify_Exit::notify notify;
	client.remote_end_point_.sendNotification(notify);
	{
		td_initialize::request req;
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
	return 0;
}
#endif

