#pragma once
#include <string>
#include "threaded_queue.h"
#include <unordered_map>
#include "MessageIssue.h"
#include "LibLsp/lsp/lsp_diagnostic.h"
#include "LibLsp/JsonRpc/MessageJsonHandler.h"

namespace lsp {
	class ostream;
	class istream;
}

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
namespace lsp {
	// internal functionality
	namespace detail {
		template <typename T>
		struct traits {
			static constexpr bool isRequest = std::is_base_of<RequestInMessage, T>::value;
			static constexpr bool isResponse = std::is_base_of<lsResponseMessage, T>::value;
			static constexpr bool isEvent = std::is_base_of<NotificationInMessage, T>::value;
		};

		// ArgTy<F>::type resolves to the first argument type of the function F.
		// F can be a function, static member function, or lambda.
		template <typename F>
		struct ArgTy {
			using type = typename ArgTy<decltype(&F::operator())>::type;
		};

		template <typename R, typename Arg>
		struct ArgTy<R(*)(Arg)> {
			using type = typename std::decay<Arg>::type;
		};

		template <typename R, typename C, typename Arg>
		struct ArgTy<R(C::*)(Arg) const> {
			using type = typename std::decay<Arg>::type;
		};
	}  // namespace detail



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

		T response;
		Rsp_Error error;  // empty represents success.
	};

	template <typename T>
	ResponseOrError<T>::ResponseOrError(const T& resp) : response(resp) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(T&& resp) : response(std::move(resp)) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(const Rsp_Error& err) : error(err) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(Rsp_Error&& err) : error(std::move(err)) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(const ResponseOrError& other)
		: response(other.response), error(other.error) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(ResponseOrError&& other)
		: response(std::move(other.response)), error(std::move(other.error)) {}
	template <typename T>
	ResponseOrError<T>& ResponseOrError<T>::operator=(
		const ResponseOrError& other) {
		response = other.response;
		error = other.error;
		return *this;
	}
	template <typename T>
	ResponseOrError<T>& ResponseOrError<T>::operator=(ResponseOrError&& other) {
		response = std::move(other.response);
		error = std::move(other.error);
		return *this;
	}

}
class RemoteEndPoint :MessageIssueHandler
{
	template <typename T>
	using IsRequest = typename std::enable_if<lsp::detail::traits<T>::isRequest>::type;
	template <typename F>
	using ArgTy = typename lsp::detail::ArgTy<F>::type;
public:
	using RequestCallFun = std::function< bool(std::unique_ptr<LspMessage>) >;
	RemoteEndPoint(std::shared_ptr < MessageJsonHandler> json_handler,
		std::shared_ptr < Endpoint> localEndPoint, lsp::Log& _log,uint8_t max_workers = 2);
	~RemoteEndPoint() override;

	template <typename F, typename RequestType = ArgTy<F>>
	void  registerRequestHandler(F&& handler) {
		using ResponseType = typename RequestType::_Response;
		
		 std::string method = RequestType::kMethodInfo;
		
		{
			std::lock_guard<std::mutex> lock(m_sendMutex);
			auto findIt = jsonHandler->method2request.find(method);
			if (findIt == jsonHandler->method2request.end()) {
				jsonHandler->method2request[method] = [](Reader& visitor)
				{
					return RequestType::ReflectReader(visitor);
				};
			}
		}

		local_endpoint->registerRequestHandler(method, [&](std::unique_ptr<LspMessage> msg) {
			lsp::ResponseOrError<ResponseType> res(handler(*reinterpret_cast<const RequestType*>(msg.get())));
			if (res.error) {
				sendResponse(res.error);
			}
			else
			{
				sendResponse(res.response);
			}
			return  true;
			});
	}
	template <typename F, typename NotifyType = ArgTy<F>>
	void  registerNotifyHandler(F&& handler) {
		const std::string methodInfo = NotifyType::kMethodInfo;
		{
			std::lock_guard<std::mutex> lock(m_sendMutex);
			auto findIt = jsonHandler->method2notification.find(methodInfo);
			if (findIt == jsonHandler->method2notification.end()) {
				jsonHandler->method2notification[methodInfo] = [](Reader& visitor)
				{
					return NotifyType::ReflectReader(visitor);
				};
			}
		}

		local_endpoint->registerNotifyHandler(methodInfo, [&](std::unique_ptr<LspMessage> msg) {
			handler(*reinterpret_cast<NotifyType*>(msg.get()));
			return  true;
		});
	}

	template <typename T>
	void sendRequest(T& request, RequestCallFun call_fun)
	{
		{
			std::lock_guard<std::mutex> lock(m_sendMutex);
			std::string method = request.GetMethodType();
			auto findIt = jsonHandler->method2response.find(method);
			if (findIt == jsonHandler->method2response.end()) {
				jsonHandler->method2response[method] = [](Reader& visitor)
				{
					using Response = typename T::_Response;
					return Response::ReflectReader(visitor);
				};
			}
		}
		internalSendRequest(request, call_fun);
	}
	template <typename T>
	std::unique_ptr<LspMessage> waitResponse(T& request, unsigned time_out = 0)
	{
		{
			std::lock_guard<std::mutex> lock(m_sendMutex);
			std::string method = request.GetMethodType();
			auto findIt = jsonHandler->method2response.find(method);
			if (findIt == jsonHandler->method2response.end()) {
				jsonHandler->method2response[method] = [](Reader& visitor)
				{
					using Response = typename T::_Response;
					return Response::ReflectReader(visitor);
				};
			}
		}
		return internalWaitResponse(request, time_out);
	}
	void sendNotification(NotificationInMessage& msg);
	void sendResponse(lsResponseMessage& msg);
	

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
	
	void internalSendRequest(RequestInMessage&, RequestCallFun);

	std::shared_ptr < std::thread > message_producer_thread_;

public:
	void handle(std::vector<MessageIssue>&&) override;
	void handle(MessageIssue&&) override;
private:

	void sendMsg(LspMessage& msg);

	void removeRequestInfo(int _id);
	void consumer(std::string&&);
	
	void mainLoop(std::unique_ptr<LspMessage>);
	bool dispatch(const std::string&);

private:
	RemoteEndPointData* d_ptr;
	StreamMessageProducer* message_producer;
	
	
	std::shared_ptr < MessageJsonHandler> jsonHandler;
	std::mutex m_sendMutex;
	std::mutex m_requsetInfo;
	
	std::shared_ptr < Endpoint> local_endpoint;

	std::unordered_map <int, PendingRequestInfo >  _client_request_futures;
	std::unordered_map <int, LspMessage* >  receivedRequestMap;
	const PendingRequestInfo* const GetRequestInfo(int _id);
	std::atomic<bool> quit{};
	std::shared_ptr<lsp::istream>  input;
	std::shared_ptr<lsp::ostream>  output;
	lsp::Log& log;

};
