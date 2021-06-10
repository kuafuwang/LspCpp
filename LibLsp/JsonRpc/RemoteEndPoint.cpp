

#include "MessageJsonHandler.h"
#include "Endpoint.h"
#include "message.h"

#include "RemoteEndPoint.h"

#include <future>

#include "Cancellation.h"
#include "StreamMessageProducer.h"
#include "NotificationInMessage.h"
#include "lsResponseMessage.h"
#include "Condition.h"
#include "rapidjson/error/en.h"
#include "json.h"
#include "ScopeExit.h"
#include "stream.h"
#include "third_party/threadpool/boost/threadpool.hpp"
using namespace  lsp;



class PendingRequestInfo
{
	using   RequestCallBack = std::function< bool(std::unique_ptr<LspMessage>) >;
public:
	PendingRequestInfo(const std::string& md,
		const RequestCallBack& callback);
	PendingRequestInfo(const std::string& md);
	PendingRequestInfo() {}
	std::string method;
	RequestCallBack futureInfo;
};

PendingRequestInfo::PendingRequestInfo(const std::string& _md,
	const	RequestCallBack& callback) : method(_md),
	futureInfo(callback)
{
}

PendingRequestInfo::PendingRequestInfo(const std::string& md) : method(md)
{
}
// Determines the encoding used to measure offsets and lengths of source in LSP.
enum class OffsetEncoding {
	// Any string is legal on the wire. Unrecognized encodings parse as this.
	UnsupportedEncoding,
	// Length counts code units of UTF-16 encoded text. (Standard LSP behavior).
	UTF16,
	// Length counts bytes of UTF-8 encoded text. (Clangd extension).
	UTF8,
	// Length counts codepoints in unicode text. (Clangd extension).
	UTF32,
};

struct RemoteEndPoint::Data
{
	explicit Data(lsp::Log& _log , RemoteEndPoint* owner)
		: message_producer(new StreamMessageProducer(*owner)), log(_log)
	{
	}
	~Data()
	{
	   delete	message_producer;
	}
	std::atomic<unsigned> m_id=0;
	boost::threadpool::pool tp;
	// Method calls may be cancelled by ID, so keep track of their state.
 // This needs a mutex: handlers may finish on a different thread, and that's
 // when we clean up entries in the map.
	mutable std::mutex request_cancelers_mutex;
	
	std::map< lsRequestId, std::pair<Canceler, /*Cookie*/ unsigned> > requestCancelers;
	
	std::atomic<unsigned>  next_request_cookie = 0; // To disambiguate reused IDs, see below.
	void onCancel(Notify_Cancellation::notify* notify) {
		std::lock_guard<std::mutex> Lock(request_cancelers_mutex);
		const auto it = requestCancelers.find(notify->params.id);
		if (it != requestCancelers.end())
			it->second.first(); // Invoke the canceler.
	}
	Key<OffsetEncoding> kCurrentOffsetEncoding;
	Context handlerContext() const {
		return Context::current().derive(
			kCurrentOffsetEncoding,
			OffsetEncoding::UTF16);
	}

	// We run cancelable requests in a context that does two things:
	//  - allows cancellation using requestCancelers[ID]
	//  - cleans up the entry in requestCancelers when it's no longer needed
	// If a client reuses an ID, the last wins and the first cannot be canceled.
	Context cancelableRequestContext(lsRequestId id) {
		auto task = cancelableTask(
			/*Reason=*/static_cast<int>(lsErrorCodes::RequestCancelled));
		unsigned cookie;
		{
			std::lock_guard<std::mutex> Lock(request_cancelers_mutex);
			cookie = next_request_cookie.fetch_add(1, std::memory_order_relaxed);
			requestCancelers[id] = { std::move(task.second), cookie };
		}
		// When the request ends, we can clean up the entry we just added.
		// The cookie lets us check that it hasn't been overwritten due to ID
		// reuse.
		return task.first.derive(lsp::make_scope_exit([this, id, cookie] {
			std::lock_guard<std::mutex> lock(request_cancelers_mutex);
			const auto& it = requestCancelers.find(id);
			if (it != requestCancelers.end() && it->second.second == cookie)
				requestCancelers.erase(it);
			}));
	}
	
	std::map <lsRequestId, std::shared_ptr<PendingRequestInfo>>  _client_request_futures;
	StreamMessageProducer* message_producer;
	std::atomic<bool> quit{};
	lsp::Log& log;
	std::shared_ptr<lsp::istream>  input;
	std::shared_ptr<lsp::ostream>  output;

	void pendingRequest(RequestInMessage& info, GenericResponseHandler&& handler)
	{
		auto id = m_id.fetch_add(1, std::memory_order_relaxed);
		info.id.set(id);
		std::lock_guard<std::mutex> lock2(m_requsetInfo);
		_client_request_futures[info.id] = std::make_shared<PendingRequestInfo>(info.method, handler);
		
	}
	const std::shared_ptr<const PendingRequestInfo> getRequestInfo(const lsRequestId& _id)
	{
		std::lock_guard<std::mutex> lock(m_requsetInfo);
		auto findIt = _client_request_futures.find(_id);
		if (findIt != _client_request_futures.end())
		{
			return findIt->second;
		}
		return  nullptr;
	}

	std::mutex m_requsetInfo;
	void removeRequestInfo(const lsRequestId& _id)
	{
		std::lock_guard<std::mutex> lock(m_requsetInfo);
		auto findIt = _client_request_futures.find(_id);
		if (findIt != _client_request_futures.end())
		{
			_client_request_futures.erase(findIt);
		}
	}
	void clear()
	{
		{
			std::lock_guard<std::mutex> lock(m_requsetInfo);
			_client_request_futures.clear();

		}
		tp.clear();
		quit.store(true, std::memory_order_relaxed);
	}
};

namespace 
{
void WriterMsg(std::shared_ptr<lsp::ostream>&  output, LspMessage& msg)
{
	const auto& s = msg.ToJson();
	const auto header =
		std::string("Content-Length: ") + std::to_string(s.size()) + "\r\n\r\n";
	output->write(header);
	output->write(s);
	output->flush();
}

bool isResponseMessage(JsonReader& visitor)
{

	if (!visitor.HasMember("id"))
	{
		return false;
	}
	
	if (!visitor.HasMember("result") && !visitor.HasMember("error"))
	{
		return false;
	}

	return true;
}

bool isRequestMessage(JsonReader& visitor)
{
	if (!visitor.HasMember("method"))
	{
		return false;
	}
	if (!visitor["method"]->IsString())
	{
		return false;
	}
	if (!visitor.HasMember("id"))
	{
		return false;
	}
	return true;
}
bool isNotificationMessage(JsonReader& visitor)
{
	if (!visitor.HasMember("method"))
	{
		return false;
	}
	if (!visitor["method"]->IsString())
	{
		return false;
	}
	if (visitor.HasMember("id"))
	{
		return false;
	}
	return true;
}
}
RemoteEndPoint::RemoteEndPoint(
	const std::shared_ptr < MessageJsonHandler >& json_handler,const std::shared_ptr < Endpoint>& localEndPoint, lsp::Log& _log, uint8_t max_workers):
    d_ptr(new Data(_log,this)),jsonHandler(json_handler), local_endpoint(localEndPoint)
{
	d_ptr->quit.store(false, std::memory_order_relaxed);
	d_ptr->tp.size_controller().resize(max_workers);
}

RemoteEndPoint::~RemoteEndPoint()
{
	delete d_ptr;
	d_ptr->quit.store(true, std::memory_order_relaxed);
}

bool RemoteEndPoint::dispatch(const std::string& content)
{
		rapidjson::Document document;
		document.Parse(content.c_str(), content.length());
		if (document.HasParseError())
		{
			std::string info ="lsp msg format error:";
			rapidjson::GetParseErrorFunc GetParseError = rapidjson::GetParseError_En; // or whatever
			info+= GetParseError(document.GetParseError());
			info += "\n";
			info += "ErrorContext offset:\n";
			info += content.substr(document.GetErrorOffset());
			d_ptr->log.log(Log::Level::SEVERE, info);
		
			return false;
		}

		JsonReader visitor{ &document };
		if (!visitor.HasMember("jsonrpc") ||
			std::string(visitor["jsonrpc"]->GetString()) != "2.0") 
		{
			std::string reason;
			reason = "Reason:Bad or missing jsonrpc version\n";
			reason += "content:\n" + content;
			d_ptr->log.log(Log::Level::SEVERE, reason);
			return  false;
	
		}
		LspMessage::Kind _kind = LspMessage::NOTIFICATION_MESSAGE;
		try {
			if (isRequestMessage(visitor))
			{
				_kind = LspMessage::REQUEST_MESSAGE;
				auto msg = jsonHandler->parseRequstMessage(visitor["method"]->GetString(), visitor);
				if (msg) {
					mainLoop(std::move(msg));
				}
				else {
					std::string info = "Unknown support request message when consumer message:\n";
					info += content;
					d_ptr->log.log(Log::Level::WARNING, info);
					return false;
				}
			}
			else if (isResponseMessage(visitor))
			{
				_kind = LspMessage::RESPONCE_MESSAGE;
				lsRequestId id;
				ReflectMember(visitor, "id", id);

				auto msgInfo = d_ptr->getRequestInfo(id);
				if (!msgInfo)
				{
					std::pair<std::string, std::unique_ptr<LspMessage>> result;
					auto b = jsonHandler->resovleResponseMessage(visitor, result);
					if (b)
					{
						result.second->SetMethodType(result.first.c_str());
						d_ptr->tp.schedule([]() {});
					}
					else
					{
						std::string info = "Unknown response message :\n";
						info += content;
						d_ptr->log.log(Log::Level::INFO, info);
					}
				}
				else
				{

					auto msg = jsonHandler->parseResponseMessage(msgInfo->method, visitor);
					if (msg) {
						mainLoop(std::move(msg));
					}
					else
					{
						std::string info = "Unknown response message :\n";
						info += content;
						d_ptr->log.log(Log::Level::SEVERE, info);
						return  false;
					}

				}
			}
			else if (isNotificationMessage(visitor))
			{
				auto msg = jsonHandler->parseNotificationMessage(visitor["method"]->GetString(), visitor);
				if (!msg)
				{
					std::string info = "Unknown notification message :\n";
					info += content;
					d_ptr->log.log(Log::Level::SEVERE, info);
					return  false;
				}
				mainLoop(std::move(msg));
			}
			else
			{
				std::string info = "Unknown lsp message when consumer message:\n";
				info += content;
				d_ptr->log.log(Log::Level::WARNING, info);
				return false;
			}
		}
		catch (std::exception& e)
		{

			std::string info = "Exception  when process ";
			if(_kind==LspMessage::REQUEST_MESSAGE)
			{
				info += "request";
			}
			if (_kind == LspMessage::RESPONCE_MESSAGE)
			{
				info += "response";
			}
			else
			{
				info += "notification";
			}
			info += " message:\n";
			info += e.what();
			std::string reason = "Reason:" + info + "\n";
			reason += "content:\n" + content;
			d_ptr->log.log(Log::Level::SEVERE, reason);
			return false;
		}
	return  true;
}



void RemoteEndPoint::internalSendRequest( RequestInMessage& info, GenericResponseHandler handler)
{
	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (!d_ptr->output && d_ptr->output->bad())
	{
		std::string desc = "Output isn't good any more:\n";
		d_ptr->log.log(Log::Level::INFO, desc);
		return ;
	}
	d_ptr->pendingRequest(info, std::move(handler));
	WriterMsg(d_ptr->output, info);
}

void RemoteEndPoint::sendNotification( NotificationInMessage& msg)
{
	sendMsg(msg);
}

std::unique_ptr<LspMessage> RemoteEndPoint::internalWaitResponse(RequestInMessage& request, unsigned time_out)
{
	auto  eventFuture = std::make_shared< Condition< LspMessage > >();
	internalSendRequest(request, [=](std::unique_ptr<LspMessage> data)
	{
		eventFuture->notify(std::move(data));
		return  true;
	});
	return   eventFuture->wait(time_out);
}
void RemoteEndPoint::sendResponse( lsResponseMessage& msg)
{
	sendMsg(msg);
}
void RemoteEndPoint::mainLoop(std::unique_ptr<LspMessage>msg)
{
	if(d_ptr->quit.load(std::memory_order_relaxed))
	{
		return;
	}
	const auto _kind = msg->GetKid();
	if (_kind == LspMessage::REQUEST_MESSAGE)
	{
		auto req = dynamic_cast<RequestInMessage*>(msg.get());
	
		WithContext HandlerContext(d_ptr->handlerContext());
		// Calls can be canceled by the client. Add cancellation context.
		WithContext WithCancel(d_ptr->cancelableRequestContext(req->id));
		local_endpoint->onRequest(std::move(msg));
	}

	else if (_kind == LspMessage::RESPONCE_MESSAGE)
	{
		auto response = dynamic_cast<lsResponseMessage*>(msg.get());
		auto msgInfo = d_ptr->getRequestInfo(response->id);
		if (!msgInfo)
		{
			const auto _method_desc = msg->GetMethodType();
			local_endpoint->onResponse(_method_desc, std::move(msg));
		}
		else
		{
			bool needLocal = true;
			if (msgInfo->futureInfo)
			{
				if (msgInfo->futureInfo(std::move(msg)))
				{
					needLocal = false;
				}
			}
			if (needLocal)
			{
				local_endpoint->onResponse(msgInfo->method, std::move(msg));
			}
			d_ptr->removeRequestInfo(response->id);
		}
	}
	else if (_kind == LspMessage::NOTIFICATION_MESSAGE)
	{
		if (msg->GetMethodType() == Notify_Cancellation::notify::kMethodInfo)
		{
			d_ptr->onCancel(reinterpret_cast<Notify_Cancellation::notify*>(msg.get()));
		}
		else
		{
			local_endpoint->notify(std::move(msg));
		}

	}
	else
	{
		std::string info = "Unknown lsp message  when process  message  in mainLoop:\n";
		d_ptr->log.log(Log::Level::WARNING, info);
	}
}

void RemoteEndPoint::handle(std::vector<MessageIssue>&& issue)
{
	for(auto& it : issue)
	{
		d_ptr->log.log(it.code, it.text);
	}
}

void RemoteEndPoint::handle(MessageIssue&& issue)
{
	d_ptr->log.log(issue.code, issue.text);
}


void RemoteEndPoint::startProcessingMessages(std::shared_ptr<lsp::istream> r,
	std::shared_ptr<lsp::ostream> w)
{
	d_ptr->quit.store(false, std::memory_order_relaxed);
	d_ptr->input = r;
	d_ptr->output = w;
	d_ptr->message_producer->bind(r);
	message_producer_thread_ = std::make_shared<std::thread>([&]()
   {
		d_ptr->message_producer->listen([&](std::string&& content){
				auto temp = std::make_shared<std::string>();
				temp->swap(content);
				d_ptr->tp.schedule([=]{
						dispatch(*temp);
				});
		});
	});
}

void RemoteEndPoint::Stop()
{
	if(message_producer_thread_ && message_producer_thread_->joinable())
	{
		message_producer_thread_->detach();
	}
	d_ptr->clear();

}

void RemoteEndPoint::sendMsg( LspMessage& msg)
{

	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (!d_ptr->output && d_ptr->output->bad())
	{
		std::string info = "Output isn't good any more:\n";
		d_ptr->log.log(Log::Level::INFO, info);
		return;
	}
	WriterMsg(d_ptr->output, msg);

}
