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

class RemoteEndPoint :MessageIssueHandler
{
public:
	using RequestCallFun = std::function< bool(std::unique_ptr<LspMessage>) >;
	RemoteEndPoint(std::istream& in, std::ostream& out, MessageJsonHandler& json_handler, Endpoint& localEndPoint, lsp::Log& _log);
	~RemoteEndPoint();
	bool consumer(std::string&&);
	std::unique_ptr<LspMessage> waitResponse(RequestInMessage&,unsigned time_out= 0);
	long sendRequest( RequestInMessage&, RequestCallFun);
	long sendRequest( RequestInMessage&);
	void sendNotification( NotificationInMessage& msg);
	void sendResponse( lsResponseMessage& msg);
	void sendMsg( LspMessage& msg);
	

	void StartThread();
	void StopThread();
	void removeRequestInfo(int _id);

private:

	std::unordered_map <int, PendingRequestInfo >  _client_request_futures;
	std::unordered_map <int, LspMessage* >  receivedRequestMap;
	const PendingRequestInfo* const GetRequestInfo(int _id);
	std::atomic<bool> quit{};
	std::istream& input;
	std::ostream& output;
	lsp::Log& log;
	void mainLoop();
public:
	void handle(std::vector<MessageIssue>&&) override;
	void handle(MessageIssue&&) override;
private:
	StreamMessageProducer* message_producer;
	
	std::shared_ptr < MultiQueueWaiter> request_waiter;
	ThreadedQueue< std::unique_ptr<LspMessage> > on_request;

	MessageJsonHandler& jsonHandler;
	std::mutex m_sendMutex;
	std::mutex m_requsetInfo;
	
	Endpoint& local_endpoint;
	long  m_generate = 0;
};
