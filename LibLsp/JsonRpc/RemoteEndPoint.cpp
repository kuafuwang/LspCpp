

#include "MessageJsonHandler.h"
#include "Endpoint.h"
#include "message.h"

#include "RemoteEndPoint.h"

#include <future>

#include "cancellation.h"
#include "StreamMessageProducer.h"
#include "NotificationInMessage.h"
#include "lsResponseMessage.h"
#include "Condition.h"
#include "PendingRequestInfo.h"
#include "rapidjson/error/en.h"
#include "json.h"
#include <boost/threadpool.hpp>
using namespace  lsp;

struct RemoteEndPointData
{
	boost::threadpool::pool tp;
};
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

RemoteEndPoint::RemoteEndPoint(std::istream& in, std::ostream& out,
	MessageJsonHandler& json_handler, Endpoint& localEndPoint, lsp::Log& _log, uint8_t max_workers):
	input(in),
	output(out),
    jsonHandler(json_handler),log(_log),local_endpoint(localEndPoint)
{
	message_producer = new StreamMessageProducer(*this, in);
	quit.store(false, std::memory_order_relaxed);
	d_ptr = new RemoteEndPointData();
	d_ptr->tp.size_controller().resize(max_workers);
}

RemoteEndPoint::~RemoteEndPoint()
{
	delete message_producer;
	quit.store(true, std::memory_order_relaxed);

	
}
void  RemoteEndPoint::consumer(std::string&& content)
{
	auto temp= std::make_shared<std::string>();
	temp->swap(content);
	d_ptr->tp.schedule([=]
	{
		dispatch(*temp);
	});
}
bool RemoteEndPoint::dispatch(const std::string& content)
{
		//log.log(Log::Level::SEVERE, content);
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
			log.log(Log::Level::SEVERE, info);
		
			return false;
		}

		JsonReader visitor{ &document };
		if (!visitor.HasMember("jsonrpc") ||
			std::string(visitor["jsonrpc"]->GetString()) != "2.0") 
		{
			std::string reason;
			reason = "Reason:Bad or missing jsonrpc version\n";
			reason += "content:\n" + content;
			log.log(Log::Level::SEVERE, reason);
			return  false;
	
		}
		if (isRequestMessage(visitor))
		{
			auto msg = jsonHandler.parseRequstMessage(visitor["method"]->GetString(), visitor);
			if (msg) {
				auto temp = reinterpret_cast<RequestInMessage*>(msg.get());
				{
					std::lock_guard<std::mutex> lock(m_sendMutex);
					receivedRequestMap[temp->id.value] = msg.get();
				}
				mainLoop(std::move(msg));
			
			}
			else {
				std::string info = "Unknown support request message when consumer message:\n";
				info += content;
				log.log(Log::Level::WARNING, info);
				return false;
			}
		}
		else if (isResponseMessage(visitor))
		{
			// 找到对应的request ,然后执行handler
			
			try
			{
				lsRequestId id;
				ReflectMember(visitor, "id", id);
		
				auto msgInfo = GetRequestInfo(id.value);
				if (!msgInfo)
				{
					std::pair<std::string, std::unique_ptr<LspMessage>> result;
					auto b = jsonHandler.resovleResponseMessage(visitor, result);
					if (b)
					{
						result.second->SetMethodType(result.first.c_str());
						d_ptr->tp.schedule([]() {});
					}
					else
					{
						std::string info = "Unknown response message :\n";
						info += content;
						log.log(Log::Level::INFO, info);
					}

				}
				else
				{
					
					auto msg = jsonHandler.parseResponseMessage(msgInfo->method, visitor);
					if(msg){
						mainLoop(std::move(msg));
					}
					else
					{
						std::string info = "Unknown response message :\n";
						info += content;
						log.log(Log::Level::SEVERE, info);
						return  false;
					}

				}
			}
			catch (std::exception& e)
			{
				std::string info = "Exception  when process response message:\n";
				info += e.what();
				std::string reason;
				reason = "Reason:" + info + "\n";
				reason += "content:\n" + content;
				log.log(Log::Level::SEVERE, reason);
				return false;
			}
		}
		else if (isNotificationMessage(visitor))
		{
			// 调用notification handler来处理
			try
			{
				auto msg = jsonHandler.parseNotificationMessage(visitor["method"]->GetString(), visitor);
				if(!msg)
				{
					std::string info = "Unknown notification message :\n";
					info += content;
					log.log(Log::Level::SEVERE, info);
					return  false;
				}
				if(msg->GetMethodType() == Notify_Cancellation::kMethodType)
				{
					
					auto temp = reinterpret_cast<Notify_Cancellation::notify*>(msg.get());
					std::unique_ptr<LspMessage> requestMsg;
					{
						std::lock_guard<std::mutex> lock(m_sendMutex);
						auto findIt = receivedRequestMap.find(temp->params.id.value);
						if (findIt != receivedRequestMap.end())
						{
							
							receivedRequestMap.erase(findIt);
						}
					}
				}
				else
				{
					mainLoop(std::move(msg));
				}
			}
			catch (std::exception& e)
			{
				std::string info = "Exception  when process notification message:\n";
				info += e.what();
				std::string reason = "Reason:" + info + "\n";
				reason += "content:\n" + content;
				log.log(Log::Level::SEVERE, reason);
				return false;
			}
		}
		else
		{
			std::string info = "Unknown lsp message when consumer message:\n";
			info += content;
			log.log(Log::Level::WARNING, info);
			return false;
		}

	return  true;
}

long RemoteEndPoint::sendRequest( RequestInMessage& info)
{
	return sendRequest(info, nullptr);
}

long RemoteEndPoint::sendRequest( RequestInMessage& info, RequestCallFun call_fun)
{
	{
		std::lock_guard<std::mutex> lock(m_sendMutex);
		if (!output || !output.good())
		{
			std::string info = "output isn't good any more:\n";
			log.log(Log::Level::INFO, info);
			return -1;
		}
		::_InterlockedIncrement(&m_generate);
		info.id.set(m_generate);
		std::lock_guard<std::mutex> lock2(m_requsetInfo);
		_client_request_futures[info.id.value] = PendingRequestInfo(info.method, call_fun);
		const auto s = info.ToJson();
		output << "Content-Length: " << s.size() << "\r\n\r\n" << s;
		output.flush();
		return  m_generate;
	}
}

void RemoteEndPoint::sendNotification( NotificationInMessage& msg)
{
	sendMsg(msg);
}

std::unique_ptr<LspMessage> RemoteEndPoint::waitResponse(RequestInMessage& request, unsigned time_out)
{
	auto  eventFuture = std::make_shared< Condition< LspMessage > >();

	sendRequest(request, [=](std::unique_ptr<LspMessage> data)
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

const PendingRequestInfo* const  RemoteEndPoint::GetRequestInfo(int _id)
{
	std::lock_guard<std::mutex> lock(m_requsetInfo);
	auto findIt = _client_request_futures.find(_id);
	if (findIt != _client_request_futures.end())
	{
		return &(findIt->second);
	}
	return  nullptr;
}

void RemoteEndPoint::mainLoop(std::unique_ptr<LspMessage>msg)
{
		if(quit.load(std::memory_order_relaxed))
		{
			return;
		}
		auto _kind = msg->GetKid();
		if (_kind == LspMessage::REQUEST_MESSAGE)
		{
			try
			{
				auto temp = reinterpret_cast<RequestInMessage*>(msg.get());
				{
					std::lock_guard<std::mutex> lock(m_sendMutex);
					receivedRequestMap.erase(temp->id.value);
				}
				local_endpoint.onRequest(std::move(msg));

			}
			catch (std::exception& e)
			{
				std::string info = "Exception  when process request message:\n";
				info += e.what();
				log.log(Log::Level::SEVERE, info);
			}
		}

		else if (_kind == LspMessage::RESPONCE_MESSAGE)
		{
			// 找到对应的request ,然后执行handler
			auto response = dynamic_cast<lsResponseMessage*>(msg.get());
			try
			{
				auto id = response->id.value;
				auto msgInfo = GetRequestInfo(id);
				if (!msgInfo)
				{
					local_endpoint.onResponse(msg->GetMethodType(), std::move(msg));
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
						local_endpoint.onResponse(msgInfo->method, std::move(msg));
					}
					removeRequestInfo(id);
				}
			}
			catch (std::exception& e)
			{
				std::string info = "Exception  when process response message in mainLoop:\n";
				info += e.what();
				log.log(Log::Level::SEVERE, info);
				
			}


		}
		else if (_kind == LspMessage::NOTIFICATION_MESSAGE)
		{
			// 调用notification handler来处理
			try
			{
				local_endpoint.notify(std::move(msg));
			}
			catch (std::exception& e)
			{
				std::string info = "Exception  when process notification message in mainLoop:\n";
				info += e.what();
				log.log(Log::Level::SEVERE, info);
			}
		}
		else
		{
			std::string info = "Unknown lsp message  when process  message  in mainLoop:\n";
			log.log(Log::Level::WARNING, info);
		}
}

void RemoteEndPoint::handle(std::vector<MessageIssue>&& issue)
{
	for(auto& it : issue)
	{
		log.log(it.code, it.text);
	}
}

void RemoteEndPoint::handle(MessageIssue&& issue)
{
		log.log(issue.code, issue.text);
}


void RemoteEndPoint::StartThread()
{
	message_producer_thread_ = std::make_shared<std::thread>([&]()
		{
			message_producer->listen(std::bind(&RemoteEndPoint::consumer, this, std::placeholders::_1));
		});


}

void RemoteEndPoint::StopThread()
{
	if(message_producer_thread_ && message_producer_thread_->joinable())
	{
		message_producer_thread_->detach();
	}

	d_ptr->tp.clear();
	if(!quit.load(std::memory_order_relaxed))
		quit.store(true, std::memory_order_relaxed);
}

void RemoteEndPoint::removeRequestInfo(int _id)
{
	std::lock_guard<std::mutex> lock(m_requsetInfo);
	auto findIt = _client_request_futures.find(_id);
	if (findIt != _client_request_futures.end())
	{
		_client_request_futures.erase(findIt);
	}
}

void RemoteEndPoint::sendMsg( LspMessage& msg)
{
	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (!output || !output.good())
	{
		std::string info = "output isn't good any more:\n";
		log.log(Log::Level::INFO, info);
		return;
	}
	const auto& s = msg.ToJson();
	output << "Content-Length: " << s.size() << "\r\n\r\n" << s;
	output.flush();
}
