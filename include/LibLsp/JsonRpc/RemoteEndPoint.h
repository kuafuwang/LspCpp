#pragma once
#include "LibLsp/lsp/lsp_diagnostic.h"
#include "LibLsp/JsonRpc/Cancellation.h"
#include "LibLsp/JsonRpc/RequestError.h"
#include "LibLsp/JsonRpc/lsResponseMessage.h"
#include "LibLsp/JsonRpc/RequestInMessage.h"
#include "LibLsp/JsonRpc/NotificationInMessage.h"
#include "traits.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include "threaded_queue.h"
#include <unordered_map>
#include "MessageIssue.h"
#include "LibLsp/JsonRpc/MessageJsonHandler.h"
#include "Endpoint.h"
#include "future.h"
#include <cstdint>
#include <utility>
#include "MessageProducer.h"

class MessageJsonHandler;
class Endpoint;
struct LspMessage;
class RemoteEndPoint;
struct RemoteEndPointTestAccess;
namespace lsp
{
class ostream;
class istream;

////////////////////////////////////////////////////////////////////////////////
// ResponseOrError<T>
////////////////////////////////////////////////////////////////////////////////

// ResponseOrError holds either the response to a  request or an error
// message.
template<typename T>
struct ResponseOrError
{
    using Request = T;
    ResponseOrError();
    ResponseOrError(T const& response);
    ResponseOrError(T&& response);
    ResponseOrError(Rsp_Error const& error);
    ResponseOrError(Rsp_Error&& error);
    ResponseOrError(ResponseOrError const& other);
    ResponseOrError(ResponseOrError&& other) noexcept;

    ResponseOrError& operator=(ResponseOrError const& other);
    ResponseOrError& operator=(ResponseOrError&& other) noexcept;
    bool IsError() const
    {
        return is_error;
    }
    std::string ToJson()
    {
        if (is_error)
        {
            return error.ToJson();
        }
        return response.ToJson();
    }
    T response;
    Rsp_Error error; // empty represents success.
    bool is_error;
};

template<typename T>
ResponseOrError<T>::ResponseOrError() : is_error(false)
{
}

template<typename T>
ResponseOrError<T>::ResponseOrError(T const& resp) : response(resp), is_error(false)
{
}
template<typename T>
ResponseOrError<T>::ResponseOrError(T&& resp) : response(std::move(resp)), is_error(false)
{
}
template<typename T>
ResponseOrError<T>::ResponseOrError(Rsp_Error const& err) : error(err), is_error(true)
{
}
template<typename T>
ResponseOrError<T>::ResponseOrError(Rsp_Error&& err) : error(std::move(err)), is_error(true)
{
}
template<typename T>
ResponseOrError<T>::ResponseOrError(ResponseOrError const& other)
    : response(other.response), error(other.error), is_error(other.is_error)
{
}
template<typename T>
ResponseOrError<T>::ResponseOrError(ResponseOrError&& other) noexcept
    : response(std::move(other.response)), error(std::move(other.error)), is_error(other.is_error)
{
}
template<typename T>
ResponseOrError<T>& ResponseOrError<T>::operator=(ResponseOrError const& other)
{
    response = other.response;
    error = other.error;
    is_error = other.is_error;
    return *this;
}
template<typename T>
ResponseOrError<T>& ResponseOrError<T>::operator=(ResponseOrError&& other) noexcept
{
    response = std::move(other.response);
    error = std::move(other.error);
    is_error = other.is_error;
    return *this;
}

namespace detail
{
template<typename T>
struct is_lsp_future : std::false_type
{
};

template<typename T>
struct is_lsp_future<lsp::future<T>> : std::true_type
{
};

template<typename T, typename ResponseType>
struct is_valid_async_request_return : std::false_type
{
};

template<typename ValueType, typename ResponseType>
struct is_valid_async_request_return<lsp::future<ValueType>, ResponseType>
    : std::integral_constant<
        bool,
        std::is_same<ValueType, ResponseType>::value
            || std::is_same<ValueType, lsp::ResponseOrError<ResponseType>>::value>
{
};

template<typename F>
using handler_return_t = typename lsp::traits::SignatureOfT<F>::ret;

struct AsyncResponseState
{
    std::atomic<bool> active {true};
    std::shared_ptr<lsp::ostream> output;
    std::shared_ptr<std::mutex> send_mutex;
};

template<typename ResponseType>
lsp::ResponseOrError<ResponseType> normalizeHandlerResult(lsp::ResponseOrError<ResponseType> const& result)
{
    return result;
}

template<typename ResponseType>
lsp::ResponseOrError<ResponseType> normalizeHandlerResult(lsp::ResponseOrError<ResponseType>&& result)
{
    return std::move(result);
}

template<typename ResponseType, typename Result>
typename std::enable_if<
    !std::is_same<typename std::decay<Result>::type, lsp::ResponseOrError<ResponseType>>::value,
    lsp::ResponseOrError<ResponseType>>::type
normalizeHandlerResult(Result&& result)
{
    return lsp::ResponseOrError<ResponseType>(std::forward<Result>(result));
}
} // namespace detail

} // namespace lsp

class RemoteEndPoint : MessageIssueHandler
{

    template<typename F, int N>
    using ParamType = lsp::traits::ParameterType<F, N>;

    template<typename T>
    using IsRequest = lsp::traits::EnableIfIsType<RequestInMessage, T>;

    template<typename T>
    using IsResponse = lsp::traits::EnableIfIsType<ResponseInMessage, T>;

    template<typename T>
    using IsNotify = lsp::traits::EnableIfIsType<NotificationInMessage, T>;

    template<typename F, typename RequestType, typename ResponseType>
    using EnableIfSyncRequestHandler = lsp::traits::EnableIf<
        !lsp::detail::is_lsp_future<lsp::detail::handler_return_t<F>>::value
            && lsp::traits::CompatibleWith<
                F, std::function<lsp::ResponseOrError<ResponseType>(RequestType const&)>>::value>;

    template<typename F, typename RequestType, typename ResponseType>
    using EnableIfSyncRequestHandlerWithMonitor = lsp::traits::EnableIf<
        !lsp::detail::is_lsp_future<lsp::detail::handler_return_t<F>>::value
            && lsp::traits::CompatibleWith<
                F,
                std::function<lsp::ResponseOrError<ResponseType>(RequestType const&, CancelMonitor const&)>>::value>;

    template<typename F, typename RequestType, typename ResponseType>
    using EnableIfAsyncRequestHandler = lsp::traits::EnableIf<
        lsp::detail::is_valid_async_request_return<lsp::detail::handler_return_t<F>, ResponseType>::value
            && lsp::traits::CompatibleWith<F, std::function<lsp::future<ResponseType>(RequestType const&)>>::value>;

    template<typename F, typename RequestType, typename ResponseType>
    using EnableIfAsyncRequestHandlerWithMonitor = lsp::traits::EnableIf<
        lsp::detail::is_valid_async_request_return<lsp::detail::handler_return_t<F>, ResponseType>::value
            && lsp::traits::CompatibleWith<
                F, std::function<lsp::future<ResponseType>(RequestType const&, CancelMonitor const&)>>::value>;

public:
    RemoteEndPoint(
        std::shared_ptr<MessageJsonHandler> const& json_handler, std::shared_ptr<Endpoint> const& localEndPoint,
        lsp::Log& _log, lsp::JSONStreamStyle style = lsp::JSONStreamStyle::Standard, uint8_t max_workers = 2
    );

    ~RemoteEndPoint() override;
    template<typename F, typename RequestType = ParamType<F, 0>, typename ResponseType = typename RequestType::Response>
    EnableIfSyncRequestHandler<F, RequestType, ResponseType> registerHandler(F&& handler)
    {
        registerSyncRequestHandler<F, RequestType, ResponseType>(std::forward<F>(handler));
    }
    template<typename F, typename RequestType = ParamType<F, 0>, typename ResponseType = typename RequestType::Response>
    EnableIfSyncRequestHandlerWithMonitor<F, RequestType, ResponseType> registerHandler(F&& handler)
    {
        registerSyncRequestHandlerWithMonitor<F, RequestType, ResponseType>(std::forward<F>(handler));
    }
    template<typename F, typename RequestType = ParamType<F, 0>, typename ResponseType = typename RequestType::Response>
    EnableIfAsyncRequestHandler<F, RequestType, ResponseType> registerHandler(F&& handler)
    {
        registerAsyncRequestHandler<F, RequestType, ResponseType>(std::forward<F>(handler));
    }
    template<typename F, typename RequestType = ParamType<F, 0>, typename ResponseType = typename RequestType::Response>
    EnableIfAsyncRequestHandlerWithMonitor<F, RequestType, ResponseType> registerHandler(F&& handler)
    {
        registerAsyncRequestHandlerWithMonitor<F, RequestType, ResponseType>(std::forward<F>(handler));
    }
    using RequestErrorCallback = std::function<void(Rsp_Error const&)>;

    template<typename T, typename F, typename ResponseType = ParamType<F, 0>>
    void send(T& request, F&& handler, RequestErrorCallback onError)
    {
        registerResponseJsonHandlerIfMissing<T>();
        auto cb = [=](std::unique_ptr<LspMessage> msg)
        {
            if (!msg)
            {
                return true;
            }
            auto const result = msg.get();

            if (static_cast<ResponseInMessage*>(result)->IsErrorType())
            {
                auto const rsp_error = static_cast<Rsp_Error const*>(result);
                onError(*rsp_error);
            }
            else
            {
                handler(*static_cast<ResponseType*>(result));
            }

            return true;
        };
        if (!internalSendRequest(request, cb))
        {
            Rsp_Error error;
            error.id = request.id;
            error.error.code = lsErrorCodes::InternalError;
            error.error.message = "Failed to send request.";
            onError(error);
        }
    }

    template<typename F, typename NotifyType = ParamType<F, 0>>
    IsNotify<NotifyType> registerHandler(F&& handler)
    {
        {
            std::lock_guard<std::mutex> lock(*m_sendMutex);
            if (!jsonHandler->GetNotificationJsonHandler(NotifyType::kMethodInfo))
            {
                jsonHandler->SetNotificationJsonHandler(
                    NotifyType::kMethodInfo, [](Reader& visitor) { return NotifyType::ReflectReader(visitor); }
                );
            }
        }
        local_endpoint->registerNotifyHandler(
            NotifyType::kMethodInfo,
            [=](std::unique_ptr<LspMessage> msg)
            {
                handler(*static_cast<NotifyType*>(msg.get()));
                return true;
            }
        );
    }

    template<typename T, typename = IsRequest<T>>
    lsp::future<lsp::ResponseOrError<typename T::Response>> send(T& request)
    {

        registerResponseJsonHandlerIfMissing<T>();
        using Response = typename T::Response;
        auto promise = std::make_shared<lsp::promise<lsp::ResponseOrError<Response>>>();
        auto cb = [=](std::unique_ptr<LspMessage> msg)
        {
            if (!msg)
            {
                return true;
            }
            auto result = msg.get();

            if (static_cast<ResponseInMessage*>(result)->IsErrorType())
            {
                Rsp_Error* rsp_error = static_cast<Rsp_Error*>(result);
                Rsp_Error temp;
                std::swap(temp, *rsp_error);
                promise->set_value(std::move(lsp::ResponseOrError<Response>(std::move(temp))));
            }
            else
            {
                Response temp;
                std::swap(temp, *static_cast<Response*>(result));
                promise->set_value(std::move(lsp::ResponseOrError<Response>(std::move(temp))));
            }
            return true;
        };
        if (!internalSendRequest(request, cb))
        {
            Rsp_Error error;
            error.id = request.id;
            error.error.code = lsErrorCodes::InternalError;
            error.error.message = "Failed to send request.";
            promise->set_value(lsp::ResponseOrError<Response>(std::move(error)));
        }
        return promise->get_future();
    }

    template<typename T, typename = IsRequest<T>>
    std::unique_ptr<lsp::ResponseOrError<typename T::Response>> waitResponse(T& request, unsigned const time_out = 0)
    {
        auto future_rsp = send(request);
        if (time_out == 0)
        {
            future_rsp.wait();
        }
        else
        {
            auto state = future_rsp.wait_for(std::chrono::milliseconds(time_out));
            if (lsp::future_status::timeout == state)
            {
                removeRequestInfo(request.id);
                return {};
            }
        }

        using Response = typename T::Response;
        return std::make_unique<lsp::ResponseOrError<Response>>(std::move(future_rsp.get()));
    }

    void send(NotificationInMessage& msg)
    {
        sendMsg(msg);
    }

    void send(ResponseInMessage& msg)
    {
        sendMsg(msg);
    }

    void sendNotification(NotificationInMessage& msg)
    {
        send(msg);
    }
    void sendResponse(ResponseInMessage& msg)
    {
        send(msg);
    }
    template<typename T>
    T createRequest()
    {
        auto req = T();
        req.id.set(getNextRequestId());
        return req;
    }

    void overrideRequestParser(std::string const& method, GenericRequestJsonHandler handler)
    {
        std::lock_guard<std::mutex> lock(*m_sendMutex);
        jsonHandler->SetRequestJsonHandler(method, std::move(handler));
    }

    template<typename RequestType, typename = IsRequest<RequestType>>
    void overrideRequestParser()
    {
        overrideRequestParser(
            RequestType::kMethodInfo, [](Reader& visitor) { return RequestType::ReflectReader(visitor); }
        );
    }

    int getNextRequestId();

    bool cancelRequest(lsRequestId const&);

    void startProcessingMessages(std::shared_ptr<lsp::istream> r, std::shared_ptr<lsp::ostream> w);

    bool isWorking() const;
    void stop();

    std::unique_ptr<LspMessage> internalWaitResponse(RequestInMessage&, unsigned time_out = 0);

    bool internalSendRequest(RequestInMessage& info, GenericResponseHandler handler);

    void handle(std::vector<MessageIssue>&&) override;
    void handle(MessageIssue&&) override;

private:
    friend struct RemoteEndPointTestAccess;

    struct ParsedMessage
    {
        bool ok = false;
        LspMessage::Kind kind = LspMessage::NOTIFICATION_MESSAGE;
        std::unique_ptr<LspMessage> message;
    };

    std::shared_ptr<lsp::detail::AsyncResponseState> getAsyncResponseState() const;
    std::shared_ptr<void> retainRequestCancellation(lsRequestId const& id);
    void postAsyncCompletion(std::function<bool()>&& completion);
    static void sendAsyncMessage(std::shared_ptr<lsp::detail::AsyncResponseState> const& state, LspMessage& msg);
    void sendRspError(lsRequestId const& id, lsp::RequestError const& error)
    {
        Rsp_Error rsp = lsp::toRspError(id, error);
        send(rsp);
    }
    void sendInternalError(lsRequestId const& id, std::string const& message)
    {
        Rsp_Error rsp;
        rsp.id = id;
        rsp.error.code = lsErrorCodes::InternalError;
        rsp.error.message = message;
        send(rsp);
    }
    CancelMonitor getCancelMonitor(lsRequestId const&);
    void removeRequestInfo(lsRequestId const&);
    void sendMsg(LspMessage& msg);
    ParsedMessage parseAndClassify(std::string const& content);
    void mainLoopCatching(std::unique_ptr<LspMessage>, uint64_t sequence, std::string const* content);
    void routeIncoming(std::string&& content, uint64_t sequence);
    void mainLoop(std::unique_ptr<LspMessage>, uint64_t sequence);
    bool dispatch(std::string const&, uint64_t sequence);
    template<typename ResponseType>
    static void sendAsyncHandlerResult(
        std::shared_ptr<lsp::detail::AsyncResponseState> const& state,
        lsp::ResponseOrError<ResponseType>&& res, lsRequestId const& id
    )
    {
        if (res.is_error)
        {
            res.error.id = id;
            sendAsyncMessage(state, res.error);
        }
        else
        {
            res.response.id = id;
            sendAsyncMessage(state, res.response);
        }
    }
    template<typename ResponseType>
    void sendHandlerResult(lsp::ResponseOrError<ResponseType>&& res, lsRequestId const& id)
    {
        if (res.is_error)
        {
            res.error.id = id;
            send(res.error);
        }
        else
        {
            res.response.id = id;
            send(res.response);
        }
    }
    template<typename ResponseType, typename Future>
    void completeAsyncHandler(Future fut, lsRequestId const& id)
    {
        auto shared_future = std::make_shared<Future>(std::move(fut));
        auto response_state = getAsyncResponseState();
        auto cancellation_retainer = retainRequestCancellation(id);
        postAsyncCompletion(
            [id, shared_future, response_state, cancellation_retainer]() mutable
            {
                if (shared_future->wait_for(std::chrono::milliseconds(0)) != lsp::future_status::ready)
                {
                    return false;
                }
                try
                {
                    sendAsyncHandlerResult(
                        response_state,
                        lsp::detail::normalizeHandlerResult<ResponseType>(shared_future->get()),
                        id
                    );
                }
                catch (lsp::RequestError const& error)
                {
                    Rsp_Error rsp = lsp::toRspError(id, error);
                    sendAsyncMessage(response_state, rsp);
                }
                catch (std::exception const& error)
                {
                    Rsp_Error rsp;
                    rsp.id = id;
                    rsp.error.code = lsErrorCodes::InternalError;
                    rsp.error.message = error.what();
                    sendAsyncMessage(response_state, rsp);
                }
                catch (...)
                {
                    Rsp_Error rsp;
                    rsp.id = id;
                    rsp.error.code = lsErrorCodes::InternalError;
                    rsp.error.message = "Unhandled exception.";
                    sendAsyncMessage(response_state, rsp);
                }
                return true;
            }
        );
    }
    template<typename F, typename RequestType, typename ResponseType>
    void registerSyncRequestHandler(F&& handler)
    {
        registerRequestJsonHandlerIfMissing<RequestType>();
        local_endpoint->registerRequestHandler(
            RequestType::kMethodInfo,
            [this, handler = std::forward<F>(handler)](std::unique_ptr<LspMessage> msg) mutable
            {
                auto req = static_cast<RequestType const*>(msg.get());
                try
                {
                    sendHandlerResult(lsp::detail::normalizeHandlerResult<ResponseType>(handler(*req)), req->id);
                }
                catch (lsp::RequestError const& error)
                {
                    sendRspError(req->id, error);
                }
                catch (std::exception const& error)
                {
                    sendInternalError(req->id, error.what());
                }
                catch (...)
                {
                    sendInternalError(req->id, "Unhandled exception.");
                }
                return true;
            }
        );
    }
    template<typename F, typename RequestType, typename ResponseType>
    void registerSyncRequestHandlerWithMonitor(F&& handler)
    {
        registerRequestJsonHandlerIfMissing<RequestType>();
        local_endpoint->registerRequestHandler(
            RequestType::kMethodInfo,
            [this, handler = std::forward<F>(handler)](std::unique_ptr<LspMessage> msg) mutable
            {
                auto req = static_cast<RequestType const*>(msg.get());
                try
                {
                    sendHandlerResult(
                        lsp::detail::normalizeHandlerResult<ResponseType>(
                            handler(*req, getCancelMonitor(req->id))
                        ),
                        req->id
                    );
                }
                catch (lsp::RequestError const& error)
                {
                    sendRspError(req->id, error);
                }
                catch (std::exception const& error)
                {
                    sendInternalError(req->id, error.what());
                }
                catch (...)
                {
                    sendInternalError(req->id, "Unhandled exception.");
                }
                return true;
            }
        );
    }
    template<typename F, typename RequestType, typename ResponseType>
    void registerAsyncRequestHandler(F&& handler)
    {
        registerRequestJsonHandlerIfMissing<RequestType>();
        local_endpoint->registerRequestHandler(
            RequestType::kMethodInfo,
            [this, handler = std::forward<F>(handler)](std::unique_ptr<LspMessage> msg) mutable
            {
                auto req = static_cast<RequestType const*>(msg.get());
                try
                {
                    completeAsyncHandler<ResponseType>(handler(*req), req->id);
                }
                catch (lsp::RequestError const& error)
                {
                    sendRspError(req->id, error);
                }
                catch (std::exception const& error)
                {
                    sendInternalError(req->id, error.what());
                }
                catch (...)
                {
                    sendInternalError(req->id, "Unhandled exception.");
                }
                return true;
            }
        );
    }
    template<typename F, typename RequestType, typename ResponseType>
    void registerAsyncRequestHandlerWithMonitor(F&& handler)
    {
        registerRequestJsonHandlerIfMissing<RequestType>();
        local_endpoint->registerRequestHandler(
            RequestType::kMethodInfo,
            [this, handler = std::forward<F>(handler)](std::unique_ptr<LspMessage> msg) mutable
            {
                auto req = static_cast<RequestType const*>(msg.get());
                try
                {
                    completeAsyncHandler<ResponseType>(handler(*req, getCancelMonitor(req->id)), req->id);
                }
                catch (lsp::RequestError const& error)
                {
                    sendRspError(req->id, error);
                }
                catch (std::exception const& error)
                {
                    sendInternalError(req->id, error.what());
                }
                catch (...)
                {
                    sendInternalError(req->id, "Unhandled exception.");
                }
                return true;
            }
        );
    }
    template<typename RequestType, typename = IsRequest<RequestType>>
    void registerRequestJsonHandlerIfMissing()
    {
        std::lock_guard<std::mutex> lock(*m_sendMutex);
        if (!jsonHandler->GetRequestJsonHandler(RequestType::kMethodInfo))
        {
            jsonHandler->SetRequestJsonHandler(
                RequestType::kMethodInfo, [](Reader& visitor) { return RequestType::ReflectReader(visitor); }
            );
        }
    }
    template<typename T, typename = IsRequest<T>>
    void registerResponseJsonHandlerIfMissing()
    {
        using Response = typename T::Response;
        std::lock_guard<std::mutex> lock(*m_sendMutex);
        if (!jsonHandler->GetResponseJsonHandler(T::kMethodInfo))
        {
            jsonHandler->SetResponseJsonHandler(
                T::kMethodInfo,
                [](Reader& visitor)
                {
                    if (visitor.HasMember("error"))
                    {
                        return Rsp_Error::ReflectReader(visitor);
                    }
                    return Response::ReflectReader(visitor);
                }
            );
        }
    }

    struct Data;

    Data* d_ptr;

    std::shared_ptr<MessageJsonHandler> jsonHandler;
    std::shared_ptr<std::mutex> m_sendMutex {std::make_shared<std::mutex>()};

    std::shared_ptr<Endpoint> local_endpoint;

public:
    std::shared_ptr<std::thread> message_producer_thread_;
};
