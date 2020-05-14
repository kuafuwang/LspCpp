
#include <deque>
#include "sct/sct.h"
#include <boost/process/pipe.hpp>

#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include "LibLsp/lsp/general/exit.h"

using namespace boost::asio::ip;

#include <LibLsp/lsp/windows/MessageNotify.h>
#include <LibLsp/lsp/JavaExtentions/addOverridableMethods.h>
#include "ApduModelTest.h"

class CCommandView;

namespace lsp {
	class Log;
}

using namespace std;
using lsp::Log;


ApduModelTest::ApduModelTest(): m_curProtocol(0), m_cmdPort(0), m_eventPort(0),log(nullptr)
{
	proxy = std::make_shared<SmartCardTool>();
}

bool ApduModelTest::CheckBeforeLaunch()
{
	if(proxy->sct)
	{
		return 	proxy->CheckBeforeLaunch();
	}
	return false;
}

bool ApduModelTest::Valid()
{
	if (proxy->sct)return true;

	return false;
}

ApduModelTest::~ApduModelTest()
{

	 proxy = nullptr;
}

bool ApduModelTest::GetATR(std::vector<unsigned char>& atr)
{
	return proxy->GetCardInfo(CardInfoType::ATR_TYPE, atr);
}

bool ApduModelTest::Connect()
{
	return proxy->Connect();

}

bool  ApduModelTest::DisConnect()
{
	 proxy->DisConnect();
	 return true;
}

bool ApduModelTest::startLsp()
{
	
	start_scp_service();
	return true;
}

void ApduModelTest::stop()
{
	if (proxy && proxy->sct)
	{
		Notify_Exit::notify _notify;
		proxy->sct->sendNotification(_notify);
	}
	if (c)
	{
		c->terminate();
		c = nullptr;
	}
	
	if (proxy && proxy->sct)
	{
		proxy->sct->StopThread();
	}
}

bool ApduModelTest::Launch()
{
	return  proxy->Launch();
}

void ApduModelTest::TerminateLaunch()
{
	  proxy->TerminateLaunch();
}

bool ApduModelTest::DownLoadCapFile(const string& strCapFileName)
{
	return proxy->DownLoadCapFile(strCapFileName);
}

bool ApduModelTest::IsConnected()
{
	return proxy->IsConnected();
}

int ApduModelTest::GetProtocol()
{
	return m_curProtocol;
}

bool ApduModelTest::SetProtocol(int protocol)
{
	m_curProtocol = protocol;
	return proxy->SetProtocol();
}

bool ApduModelTest::GpAuth()
{
	return proxy->GpAuth();
}

bool ApduModelTest::InstallApplet(InstallAppletParams&p)
{
	return proxy->InstallApplet(p);
	
}

bool ApduModelTest::Transmit(const std::vector<unsigned char>& cmdApdu, std::vector<unsigned char>& rspApdu)
{
	return proxy->Transmit(cmdApdu, rspApdu);
}

class CCommandView
{
	
};
class SctLog :public lsp::Log
{
public:
	CCommandView& output_wnd;
	SctLog(CCommandView& wnd);

	void log(Level level, std::wstring&& msg) override;
	void log(Level level, const std::wstring& msg) override;
	void log(Level level, std::string&& msg) override;
	void log(Level level, const std::string& msg) override;
};

SctLog::SctLog(CCommandView& wnd) :output_wnd(wnd)
{

}

void SctLog::log(Level level, std::wstring&& msg)
{
	
}

void SctLog::log(Level level, const std::wstring& msg)
{
	
}
void SctLog::log(Level level, std::string&& msg)
{
	
}

void SctLog::log(Level level, const std::string& msg)
{
 
}
namespace bp = boost::process;
using namespace lsp;


void AddNotifyJsonRpcMethod(GenericEndpoint& handler, ApduModelTest*proxy)
{
	
	handler.method2notification[Notify_LogMessage::kMethodType] = [=](std::unique_ptr<LspMessage> msg)
	{

#ifdef  _DEBUG
		auto  logMsg = reinterpret_cast<Notify_LogMessage::notify*>(msg.get());
		proxy->log->log(lsp::Log::Level::INFO, logMsg->params.message);
#endif

		return true;
	};
	handler.method2notification[Notify_ShowMessage::kMethodType] = [=](std::unique_ptr<LspMessage> msg)
	{
		auto  logMsg = reinterpret_cast<Notify_ShowMessage::notify*>(msg.get());
		proxy->log->log(lsp::Log::Level::INFO, logMsg->params.message);
		return true;
	};
	



	handler.method2notification[sct_NotifyJcvmOutput::kMethodType] = [&](std::unique_ptr<LspMessage> msg)
	{
		auto  data = reinterpret_cast<sct_NotifyJcvmOutput::notify*>(msg.get());
		if (data){
			
		}

		return true;
	};
	handler.method2notification[sct_NotifyDisconnect::kMethodType] = [&](std::unique_ptr<LspMessage> msg)
	{

		auto  data = reinterpret_cast<sct_NotifyDisconnect::notify*>(msg.get());
		if (data && proxy->proxy)
		{
			proxy->proxy->connect_state = false;
		}

		return true;
	};

}

CCommandView output;
using namespace boost::asio::ip;

bool ApduModelTest::start_scp_service()
{
	
	if (!log)
	{
		log = new SctLog((output));
		local_endpoint_ = new GenericEndpoint(*log);
		AddNotifyJsonRpcMethod(*local_endpoint_, this);

	}
	if (c)return false;

	std::error_code ec;

	try
	{
	
		auto write_to_service_temp = std::make_shared<boost::process::opstream>();
		auto read_from_service_temp = std::make_shared<boost::process::ipstream>();
		
		tcp::endpoint end_point(
			address::from_string("127.0.0.1"), 8065
			);
		
		auto  socket_ = std::make_shared<tcp::iostream>();
		socket_->connect(end_point);
		if (!socket_)
		{
			string temp = "Unable to connect: "+ socket_->error().message() ;
			log->log(lsp::Log::Level::INFO, temp);
			return false;
		}
		write_to_service = socket_;
		read_from_service = socket_;
		
		
		vector<string> args;

		proxy->log = log;
		proxy->sct = std::make_shared<RemoteEndPoint>(*(read_from_service.get()), *(write_to_service.get()),
			json_handler, *local_endpoint_,*log);


		proxy->sct->StartThread();

		proxy->initialize(::GetCurrentProcessId(), SmartCardTool::V5_KIND);
		
		return true;
	}
	catch (std::exception&)
	{
		return false;
	}
	return true;
}