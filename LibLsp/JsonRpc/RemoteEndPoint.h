#pragma once
#include <string>
#include "threaded_queue.h"
#include <unordered_map>
#include "MessageIssue.h"


struct lsResponseMessage;
class PendingRequestInfo;
class StreamMessageProducer;
class MessageJsonHandler;
struct InMessage;
struct NotificationInMessage;
class Endpoint;
struct RequestInMessage;
struct LspMessage;

struct  RemoteEndPointData;

class RemoteEndPoint :MessageIssueHandler
{
public:
	using RequestCallFun = std::function< bool(std::unique_ptr<LspMessage>) >;
	RemoteEndPoint(std::istream& in, std::ostream& out, MessageJsonHandler& json_handler, Endpoint& localEndPoint, lsp::Log& _log,uint8_t max_workers = 2);
	~RemoteEndPoint();
	void consumer(std::string&&);
	std::unique_ptr<LspMessage> waitResponse(RequestInMessage&,unsigned time_out= 0);
	
	long sendRequest( RequestInMessage&, RequestCallFun);
	long sendRequest( RequestInMessage&);
	void sendNotification( NotificationInMessage& msg);
	void sendResponse( lsResponseMessage& msg);
	void sendMsg( LspMessage& msg);
	

	void StartThread();
	void StopThread();
	void removeRequestInfo(int _id);
	std::shared_ptr < std::thread > message_producer_thread_;
	
private:

	std::unordered_map <int, PendingRequestInfo >  _client_request_futures;
	std::unordered_map <int, LspMessage* >  receivedRequestMap;
	const PendingRequestInfo* const GetRequestInfo(int _id);
	std::atomic<bool> quit{};
	std::istream& input;
	std::ostream& output;
	lsp::Log& log;
	void mainLoop(std::unique_ptr<LspMessage>);
	bool dispatch(const std::string&);
public:
	void handle(std::vector<MessageIssue>&&) override;
	void handle(MessageIssue&&) override;
private:
	RemoteEndPointData* d_ptr;
	StreamMessageProducer* message_producer;
	
	
	MessageJsonHandler& jsonHandler;
	std::mutex m_sendMutex;
	std::mutex m_requsetInfo;
	
	Endpoint& local_endpoint;
	long  m_generate = 0;
};
