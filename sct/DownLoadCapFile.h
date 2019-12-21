#pragma once
#include "LibLsp/JsonRpc/RequestInMessage.h"
#include "LibLsp/lsp/lsDocumentUri.h"
#include "LibLsp/lsp/lsAny.h"
#include "LibLsp/JsonRpc/NotificationInMessage.h"

struct DownLoadCapFileParams
{
	lsDocumentUri uri;
	MAKE_SWAP_METHOD(DownLoadCapFileParams, uri);
};
MAKE_REFLECT_STRUCT(DownLoadCapFileParams, uri);

struct NormalActionResult
{
	bool state = false;
	optional<std::vector<uint8_t>> data;
	optional<std::string> info;
	MAKE_SWAP_METHOD(NormalActionResult, state, data, info);
};
MAKE_REFLECT_STRUCT(NormalActionResult, data, state, info)

DEFINE_REQUEST_RESPONSE_TYPE(sct_DownLoadCapFile, DownLoadCapFileParams, NormalActionResult);




enum class SctProtocol :uint8_t
{
	T01 = 0, T0 = 1, T1 = 2,
};
MAKE_REFLECT_TYPE_PROXY(SctProtocol);

struct ConnectParams
{
	
	SctProtocol protocol= SctProtocol::T01;
	optional<std::string> reader;
	optional<lsp::Any> data;
	MAKE_SWAP_METHOD(ConnectParams, reader, protocol,data);
};
MAKE_REFLECT_STRUCT(ConnectParams, reader, protocol, data);
DEFINE_REQUEST_RESPONSE_TYPE(sct_Connect, ConnectParams, NormalActionResult);



struct SetProtocolParams
{

	SctProtocol protocol = SctProtocol::T01;

};
MAKE_REFLECT_STRUCT(SetProtocolParams, protocol);

DEFINE_REQUEST_RESPONSE_TYPE(sct_SetProtocol, SetProtocolParams, NormalActionResult);

struct GPAuthParams
{
	optional < std::string>  scp;
	optional < std::string > key;
	optional < lsp::Any >   option;
	MAKE_SWAP_METHOD(GPAuthParams, key, scp, option);
};
MAKE_REFLECT_STRUCT(GPAuthParams, key, scp, option);
DEFINE_REQUEST_RESPONSE_TYPE(sct_gp_auth, GPAuthParams, NormalActionResult);



struct InstallAppletParams
{
	std::vector<uint8_t> package_aid;
	std::vector<uint8_t> applet_aid;
	optional < std::vector<uint8_t>> instance_aid;
	optional<std::vector<uint8_t>>  authority;
	optional<std::vector<uint8_t>>  parameters;
	MAKE_SWAP_METHOD(InstallAppletParams, package_aid, applet_aid, instance_aid, authority, parameters);
};
MAKE_REFLECT_STRUCT(InstallAppletParams, package_aid, applet_aid, instance_aid, authority, parameters);
DEFINE_REQUEST_RESPONSE_TYPE(sct_InstalllApplet, InstallAppletParams, NormalActionResult);


struct TransmitParams
{
	std::vector<unsigned char> command;
	MAKE_SWAP_METHOD(TransmitParams, command);
};
MAKE_REFLECT_STRUCT(TransmitParams, command);

DEFINE_REQUEST_RESPONSE_TYPE(sct_Transmit, TransmitParams, NormalActionResult);

DEFINE_NOTIFICATION_TYPE(sct_Disconnect,JsonNull)


struct SetWindowPosParams{
	int X = 0;
	int Y = 0;
	int cx = 100;
	int cy = 100;
	
	MAKE_SWAP_METHOD(SetWindowPosParams, X, Y, cx, cy);
};
MAKE_REFLECT_STRUCT(SetWindowPosParams, X, Y, cx, cy);
DEFINE_NOTIFICATION_TYPE(sct_SetWindowsPos, SetWindowPosParams)

struct SetWindowVisibleParams
{
	static const int  HIDE = 0;
	static const int  MINSIZE = 1;
	static const int  MAXSIZE = 2;
	static const int  NORMAL = 3;
	int state = NORMAL;
	MAKE_SWAP_METHOD(SetWindowVisibleParams, state);
};
MAKE_REFLECT_STRUCT(SetWindowVisibleParams, state);
DEFINE_NOTIFICATION_TYPE(sct_SetWindowsVisible, SetWindowVisibleParams)



enum  CardInfoType:uint32_t
{
	ATR_TYPE = 0,
	ATS_TYPE = 1,
};
MAKE_REFLECT_TYPE_PROXY(CardInfoType);


struct  GetCardInfoParams
{
	CardInfoType type_;
};
MAKE_REFLECT_STRUCT(GetCardInfoParams, type_);

DEFINE_REQUEST_RESPONSE_TYPE(sct_GetCardInfo, GetCardInfoParams, NormalActionResult);


struct JdwpInfo
{
	std::string host="127.0.0.1";
	uint32_t cmd_port = 9045;
	uint32_t event_port = 9055;
};

MAKE_REFLECT_STRUCT(JdwpInfo, host, cmd_port, event_port);

struct  LaunchResult
{
	bool state;
	optional<JdwpInfo> info;
	optional<std::string> error;
	MAKE_SWAP_METHOD(LaunchResult, state, info, error);
};
MAKE_REFLECT_STRUCT(LaunchResult, state, info, error);


struct JcvmOutputParams
{
	std::string  data;
	MAKE_SWAP_METHOD(JcvmOutputParams, data);
};
MAKE_REFLECT_STRUCT(JcvmOutputParams, data);

DEFINE_NOTIFICATION_TYPE(sct_NotifyJcvmOutput, JcvmOutputParams);


DEFINE_REQUEST_RESPONSE_TYPE(sct_Launch, JsonNull, LaunchResult);


DEFINE_REQUEST_RESPONSE_TYPE(sct_CheckBeforeLaunch, JsonNull, NormalActionResult);



DEFINE_NOTIFICATION_TYPE(sct_NotifyDisconnect, JsonNull);


DEFINE_NOTIFICATION_TYPE(sct_TerminateLaunch, JsonNull);




struct sctInitializeParams {
	// The process Id of the parent process that started
	// the server. Is null if the process has not been started by another process.
	// If the parent process is not alive then the server should exit (see exit
	// notification) its process.
	optional<int> processId;

	// User provided initialization options.
	optional<lsp::Any> initializationOptions;

};
MAKE_REFLECT_STRUCT(sctInitializeParams,processId,initializationOptions);

struct sctServerCapabilities {
	bool gp_auth = false;
	bool gp_key = false;
	MAKE_SWAP_METHOD(sctServerCapabilities, gp_auth, gp_key);
};
MAKE_REFLECT_STRUCT(sctServerCapabilities, gp_auth, gp_key);


struct stcInitializeResult
{
	sctServerCapabilities   capabilities;
};
MAKE_REFLECT_STRUCT(stcInitializeResult, capabilities);

DEFINE_REQUEST_RESPONSE_TYPE(sct_initialize, sctInitializeParams, stcInitializeResult);
