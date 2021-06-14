#pragma once
#include "LibLsp/lsp/lsp_diagnostic.h"
#include "LibLsp/JsonRpc/Cancellation.h"
#include "LibLsp/JsonRpc/lsResponseMessage.h"
#include "LibLsp/JsonRpc/RequestInMessage.h"
#include "LibLsp/JsonRpc/NotificationInMessage.h"
#include "traits.h"
#include <future>
#include <string>
#include "threaded_queue.h"
#include <unordered_map>
#include "MessageIssue.h"
#include "LibLsp/JsonRpc/MessageJsonHandler.h"
#include "Endpoint.h"


class MessageJsonHandler;
class  Endpoint;
struct LspMessage;

namespace lsp {
	class ostream;
	class istream;

	////////////////////////////////////////////////////////////////////////////////
	// ResponseOrError<T>
	////////////////////////////////////////////////////////////////////////////////

	// ResponseOrError holds either the response to a  request or an error
	// message.
	template <typename T>
	struct ResponseOrError {
		using Request = T;

		inline ResponseOrError() = default;
		inline ResponseOrError(const T& response);
		inline ResponseOrError(T&& response);
		inline ResponseOrError(const Rsp_Error& error);
		inline ResponseOrError(Rsp_Error&& error);
		inline ResponseOrError(const ResponseOrError& other);
		inline ResponseOrError(ResponseOrError&& other);

		inline ResponseOrError& operator=(const ResponseOrError& other);
		inline ResponseOrError& operator=(ResponseOrError&& other);
		bool  IsError() const { return  is_error; }
		std::string ToJson()
		{
			if (is_error) return  error.ToJson();
			return  response.ToJson();
		}
		T response;
		Rsp_Error error;  // empty represents success.
		bool is_error;

	};

	template <typename T>
	ResponseOrError<T>::ResponseOrError(const T& resp) : response(resp), is_error(false) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(T&& resp) : response(std::move(resp)), is_error(false) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(const Rsp_Error& err) : error(err), is_error(true) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(Rsp_Error&& err) : error(std::move(err)), is_error(true) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(const ResponseOrError& other)
		: response(other.response), error(other.error), is_error(other.is_error) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(ResponseOrError&& other)
		: response(std::move(other.response)), error(std::move(other.error)), is_error(other.is_error) {}
	template <typename T>
	ResponseOrError<T>& ResponseOrError<T>::operator=(
		const ResponseOrError& other) {
		response = other.response;
		error = other.error;
		is_error = other.is_error;
		return *this;
	}
	template <typename T>
	ResponseOrError<T>& ResponseOrError<T>::operator=(ResponseOrError&& other) {
		response = std::move(other.response);
		error = std::move(other.error);
		is_error = other.is_error;
		return *this;
	}

}


class RemoteEndPoint :MessageIssueHandler
{

	template <typename F, int N>
	using ParamType = lsp::traits::ParameterType<F, N>;

	template <typename T>
	using IsRequest = lsp::traits::EnableIfIsType<RequestInMessage, T>;

	template <typename T>
	using IsCancelMonitor = lsp::traits::EnableIfIsType<CancelMonitor, T>;

	
	template <typename T>
	using IsResponse = lsp::traits::EnableIfIsType<lsResponseMessage, T>;

	template <typename T>
	using IsNotify = lsp::traits::EnableIfIsType<NotificationInMessage, T>;


	template <typename F, typename CallbackType>
	using IsRequestHandler = lsp::traits::EnableIf<lsp::traits::CompatibleWith<
		F,
		std::function<CallbackType(const RequestInMessage&)>>::
		value>;

	template <typename F, typename CallbackType>
	using IsRequestHandlerWithMonitor = lsp::traits::EnableIf<lsp::traits::CompatibleWith<
		F,
		std::function<CallbackType(const RequestInMessage&,const CancelMonitor&)>>::
		value>;


public:

	
	RemoteEndPoint(const std::shared_ptr <MessageJsonHandler>& json_handler,
		const std::shared_ptr < Endpoint >& localEndPoint,
		lsp::Log& _log, uint8_t max_workers = 2);
	
	~RemoteEndPoint() override;
	template <typename F, typename RequestType = ParamType<F, 0>, typename ResponseType = typename RequestType::Response>
	IsRequestHandler< F, lsp::ResponseOrError<ResponseType> >  registerHandler(F&& handler)
	{
		ProcessRequestJsonHandler(handler);
		local_endpoint->registerRequestHandler(RequestType::kMethodInfo, [=](std::unique_ptr<LspMessage> msg) {
			auto  req = reinterpret_cast<const RequestType*>(msg.get());
			lsp::ResponseOrError<ResponseType> res(handler(*req));
			if (res.is_error) {
				res.error.id = req->id;
				send(res.error);
			}
			else
			{
				res.response.id = req->id;
				send(res.response);
			}
			return  true;
		});
	}
	template <typename F, typename RequestType = ParamType<F, 0>, typename ResponseType = typename RequestType::Response>
	IsRequestHandlerWithMonitor< F, lsp::ResponseOrError<ResponseType> >  registerHandler(F&& handler)  {
		ProcessRequestJsonHandler(handler);
		local_endpoint->registerRequestHandler(RequestType::kMethodInfo, [=](std::unique_ptr<LspMessage> msg) {
			auto  req = reinterpret_cast<const RequestType*>(msg.get());
			lsp::ResponseOrError<ResponseType> res(handler(*req , getCancelMonitor(req->id)));
			if (res.is_error) {
				res.error.id = req->id;
				send(res.error);
			}
			else
			{
				res.response.id = req->id;
				send(res.response);
			}
			return  true;
		});
	}
	using RequestErrorCallback = std::function<void(const Rsp_Error&)>;
	
	template <typename T, typename F, typename ResponseType = ParamType<F, 0> >
	void send(T& request, F&& handler, RequestErrorCallback onError)
	{
		ProcessRequestJsonHandler(handler);
		auto cb = [=](std::unique_ptr<LspMessage> msg) {
			if (!msg)
				return true;
			const auto result = msg.get();
		
			if (reinterpret_cast<lsResponseMessage*>(result)->IsErrorType()) {
				const auto rsp_error = reinterpret_cast<const Rsp_Error*>(result);
				onError(*rsp_error);
			}
			else {
				handler(*reinterpret_cast<ResponseType*>(result));
			}

			return  true;
		};
		internalSendRequest(request, cb);
	}
	

	template <typename F, typename NotifyType = ParamType<F, 0> >
	IsNotify<NotifyType>  registerHandler(F&& handler) {

		{
			std::lock_guard<std::mutex> lock(m_sendMutex);
			if (!jsonHandler->GetNotificationJsonHandler(NotifyType::kMethodInfo))
			{
				jsonHandler->SetNotificationJsonHandler(NotifyType::kMethodInfo,
					[](Reader& visitor)
					{
						return NotifyType::ReflectReader(visitor);
					});
			}
		}
		local_endpoint->registerNotifyHandler(NotifyType::kMethodInfo, [=](std::unique_ptr<LspMessage> msg) {
			handler(*reinterpret_cast<NotifyType*>(msg.get()));
			return  true;
			});
	}


	template <typename T, typename = IsRequest<T>>
	std::future< lsp::ResponseOrError<typename T::Response> > send(T& request) {

		ProcessResponseJsonHandler(request);
		using Response = typename T::Response;
		auto promise = std::make_shared< std::promise<lsp::ResponseOrError<Response>>>();
		auto cb = [=](std::unique_ptr<LspMessage> msg) {
			if (!msg)
				return true;
			auto result = msg.get();

			if (reinterpret_cast<lsResponseMessage*>(result)->IsErrorType())
			{
				Rsp_Error* rsp_error = reinterpret_cast<Rsp_Error*>(result);
				Rsp_Error temp;
				std::swap(temp, *rsp_error);
				promise->set_value(std::move(lsp::ResponseOrError<Response>(std::move(temp))));
			}
			else
			{
				Response temp;
				std::swap(temp, *reinterpret_cast<Response*>(result));
				promise->set_value(std::move(lsp::ResponseOrError<Response>(std::move(temp))));
			}
			return  true;
		};
		internalSendRequest(request, cb);
		return promise->get_future();
	}

	template <typename T, typename = IsRequest<T>>
	std::unique_ptr<lsp::ResponseOrError<typename T::Response>> waitResponse(T& request, const unsigned time_out = 0)
	{
		auto future_rsp = send(request);
		if (time_out == 0)
		{
			future_rsp.wait();
		}
		else
		{
			auto state = future_rsp.wait_for(std::chrono::milliseconds(time_out));
			if (std::future_status::timeout == state)
			{
				return {};
			}
		}

		using Response = typename T::Response;
		return std::make_unique<lsp::ResponseOrError<Response>>(std::move(future_rsp.get()));
	}

	void send(NotificationInMessage& msg)
	{
		sendMsg(msg);
	};

	void send(lsResponseMessage& msg)
	{
		sendMsg(msg);
	}
	
	void sendNotification(NotificationInMessage& msg)
	{
		send(msg);
	}
	void sendResponse(lsResponseMessage& msg)
	{
		send(msg);
	}
	void startProcessingMessages(std::shared_ptr<lsp::istream> r,
		std::shared_ptr<lsp::ostream> w);

	bool IsWorking() const
	{
		if (message_producer_thread_)
			return true;
		return  false;
	}
	void Stop();

	std::unique_ptr<LspMessage> internalWaitResponse(RequestInMessage&, unsigned time_out = 0);

	void internalSendRequest(RequestInMessage&, GenericResponseHandler);

	std::shared_ptr < std::thread > message_producer_thread_;

public:
	void handle(std::vector<MessageIssue>&&) override;
	void handle(MessageIssue&&) override;
private:
	CancelMonitor getCancelMonitor(const lsRequestId&);
	void sendMsg(LspMessage& msg);
	void mainLoop(std::unique_ptr<LspMessage>);
	bool dispatch(const std::string&);
	template <typename F, typename RequestType = ParamType<F, 0>>
	IsRequest<RequestType>  ProcessRequestJsonHandler(const F& handler) {
		std::lock_guard<std::mutex> lock(m_sendMutex);
		if (!jsonHandler->GetRequestJsonHandler(RequestType::kMethodInfo))
		{
			jsonHandler->SetRequestJsonHandler(RequestType::kMethodInfo,
				[](Reader& visitor)
				{
					return RequestType::ReflectReader(visitor);
				});
		}	
	}
	template <typename T, typename = IsRequest<T>>
	void ProcessResponseJsonHandler(T& request)
	{
		using Response = typename T::Response;
		std::lock_guard<std::mutex> lock(m_sendMutex);
		if (!jsonHandler->GetResponseJsonHandler(T::kMethodInfo))
		{
			jsonHandler->SetResponseJsonHandler(T::kMethodInfo, [](Reader& visitor)
				{
					if (visitor.HasMember("error"))
						return 	Rsp_Error::ReflectReader(visitor);
					return Response::ReflectReader(visitor);
				});
		}
	}
private:
	struct Data;

	Data* d_ptr;

	std::shared_ptr < MessageJsonHandler> jsonHandler;
	std::mutex m_sendMutex;

	std::shared_ptr < Endpoint > local_endpoint;

};
