#pragma once


#include <LibLsp/JsonRpc/RemoteEndPoint.h>
#include <LibLsp/JsonRpc/EndPoint.h>


#include <boost/process/child.hpp>
#include <boost/process/pipe.hpp>
#include <sct/sct.h>
struct InstallAppletParams;

struct DeviceInfo
{
	std::string display;
	std::string id;
	std::string detail;
};
using namespace std;
class SmartCardTool;
class ModeState;
class ApduModelTest  
{
	int m_curProtocol;

public:
	std::string m_ipAddr;

	uint16_t m_cmdPort;
	uint16_t m_eventPort;
	~ApduModelTest();
	ApduModelTest();
	bool GetATR(std::vector<unsigned char>&);;
	
	inline string GetIpAddr() { return m_ipAddr; }
	inline uint16_t GetCmdPort() { return m_cmdPort; }


	inline uint16_t GetEventPort() { return m_eventPort; }

	int GetProtocol();

	bool SetProtocol(int protocol);

	bool GpAuth();;

	bool InstallApplet(InstallAppletParams&);;
	bool DownLoadCapFile(const string& strCapFileName);


	bool IsConnected();
	string GetErrorString() { return {}; };
	
	bool Connect();
	bool DisConnect();


	bool startLsp();;
	void stop();;
	
	bool Launch();
	void TerminateLaunch();

	bool CheckBeforeLaunch();
	bool Valid();

	bool Transmit(const std::vector<unsigned char>& cmdApdu, std::vector<unsigned char>& rspApdu) ;

	lsp::Log* log;

	std::shared_ptr<std::ostream>  old_write_to_service;

	std::shared_ptr< std::istream >   old_read_from_service;

	std::shared_ptr<std::ostream>  write_to_service;

	std::shared_ptr< std::istream >   read_from_service;
	std::shared_ptr<boost::process::child> c;

	std::shared_ptr<SmartCardTool>  proxy;
	
	sct::ProtocolJsonHandler json_handler;

	GenericEndpoint* local_endpoint_;
	bool start_scp_service();

	std::atomic<bool> m_exit = false;

};
