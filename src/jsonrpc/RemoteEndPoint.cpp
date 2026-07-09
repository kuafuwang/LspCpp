#include "LibLsp/JsonRpc/MessageJsonHandler.h"
#include "LibLsp/JsonRpc/Endpoint.h"
#include "LibLsp/JsonRpc/message.h"
#include "LibLsp/JsonRpc/RemoteEndPoint.h"
#include <future>
#include <functional>
#include "LibLsp/JsonRpc/Cancellation.h"
#include "LibLsp/JsonRpc/StreamMessageProducer.h"
#include "LibLsp/JsonRpc/NotificationInMessage.h"
#include "LibLsp/JsonRpc/lsResponseMessage.h"
#include "LibLsp/JsonRpc/Condition.h"
#include "LibLsp/JsonRpc/Context.h"
#include "rapidjson/error/en.h"
#include "LibLsp/JsonRpc/json.h"
#include "LibLsp/JsonRpc/ScopeExit.h"
#include "LibLsp/JsonRpc/stream.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <thread>
#include <vector>
#ifdef LSPCPP_USE_STANDALONE_ASIO
#include <asio/thread_pool.hpp>
#include <asio/post.hpp>
#else
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
namespace asio = boost::asio;
#endif

#include "LibLsp/JsonRpc/GCThreadContext.h"

namespace lsp
{

// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

// Cancellation mechanism for long-running tasks.
//
// This manages interactions between:
//
// 1. Client code that starts some long-running work, and maybe cancels later.
//
//   std::pair<Context, Canceler> Task = cancelableTask();
//   {
//     WithContext Cancelable(std::move(Task.first));
//     Expected
//     deepThoughtAsync([](int answer){ errs() << answer; });
//   }
//   // ...some time later...
//   if (User.fellAsleep())
//     Task.second();
//
//  (This example has an asynchronous computation, but synchronous examples
//  work similarly - the Canceler should be invoked from another thread).
//
// 2. Library code that executes long-running work, and can exit early if the
//   result is not needed.
//
//   void deepThoughtAsync(std::function<void(int)> Callback) {
//     runAsync([Callback]{
//       int A = ponder(6);
//       if (getCancelledMonitor())
//         return;
//       int B = ponder(9);
//       if (getCancelledMonitor())
//         return;
//       Callback(A * B);
//     });
//   }
//
//   (A real example may invoke the callback with an error on cancellation,
//   the CancelledError is provided for this purpose).
//
// Cancellation has some caveats:
//   - the work will only stop when/if the library code next checks for it.
//     Code outside clangd such as Sema will not do this.
//   - it's inherently racy: client code must be prepared to accept results
//     even after requesting cancellation.
//   - it's Context-based, so async work must be dispatched to threads in
//     ways that preserve the context. (Like runAsync() or TUScheduler).
//

/// A canceller requests cancellation of a task, when called.
/// Calling it again has no effect.
using Canceler = std::function<void()>;

// We don't want a cancelable scope to "shadow" an enclosing one.
struct CancelState
{
    std::shared_ptr<std::atomic<int>> cancelled;
    CancelState const* parent = nullptr;
    lsRequestId id;
};
static Key<CancelState> g_stateKey;

/// Defines a new task whose cancellation may be requested.
/// The returned Context defines the scope of the task.
/// When the context is active, getCancelledMonitor() is 0 until the Canceler is
/// invoked, and equal to Reason afterwards.
/// Conventionally, Reason may be the LSP error code to return.
std::pair<Context, Canceler> cancelableTask(lsRequestId const& id, int reason = 1)
{
    assert(reason != 0 && "Can't detect cancellation if Reason is zero");
    CancelState state;
    state.id = id;
    state.cancelled = std::make_shared<std::atomic<int>>();
    state.parent = Context::current().get(g_stateKey);
    return {
        Context::current().derive(g_stateKey, state),
        [reason, cancelled(state.cancelled)] { *cancelled = reason; },
    };
}
/// If the current context is within a cancelled task, returns the reason.
/// (If the context is within multiple nested tasks, true if any are cancelled).
/// Always zero if there is no active cancelable task.
/// This isn't free (context lookup) - don't call it in a tight loop.
optional<CancelMonitor> getCancelledMonitor(lsRequestId const& id, Context const& ctx = Context::current())
{
    for (CancelState const* state = ctx.get(g_stateKey); state != nullptr; state = state->parent)
    {
        if (id != state->id)
        {
            continue;
        }
        std::shared_ptr<std::atomic<int>> const cancelled = state->cancelled;
        std::function<int()> temp = [=] { return cancelled->load(); };
        return std::move(temp);
    }

    return {};
}
} // namespace lsp

using namespace lsp;
using PendingResponseHandler = std::function<bool(std::unique_ptr<LspMessage>&)>;

class PendingRequestInfo
{
public:
    PendingRequestInfo(std::string const& md, PendingResponseHandler const& callback);
    PendingRequestInfo(std::string const& md);
    PendingRequestInfo()
    {
    }
    std::string method;
    PendingResponseHandler futureInfo;
    bool complete(std::unique_ptr<LspMessage>& msg)
    {
        if (completed.exchange(true, std::memory_order_acq_rel))
        {
            return true;
        }
        return futureInfo ? futureInfo(msg) : false;
    }

private:
    std::atomic<bool> completed {false};
};

PendingRequestInfo::PendingRequestInfo(std::string const& _md, PendingResponseHandler const& callback)
    : method(_md), futureInfo(callback)
{
}

PendingRequestInfo::PendingRequestInfo(std::string const& md) : method(md)
{
}

struct RequestCancellationRegistry
{
    using CancelKey = std::pair<lsRequestId, unsigned>;

    std::mutex mutex;
    std::map<lsRequestId, std::pair<Canceler, /*Cookie*/ unsigned>> requestCancelers;
    std::map<lsRequestId, uint64_t> pendingCancelRequests;
    std::set<lsRequestId> seenRequestIds;
    std::map<CancelKey, size_t> retainedRequestCancelers;
};

struct RemoteEndPoint::Data
{
    explicit Data(lsp::JSONStreamStyle style, uint8_t workers, lsp::Log& _log, RemoteEndPoint* owner)
        : max_workers(workers), m_id(0), next_request_cookie(0), owner(owner), log(_log)
    {
        if (style == lsp::JSONStreamStyle::Standard)
        {
            message_producer = (new LSPStreamMessageProducer(*owner));
        }
        else
        {
            message_producer = (new DelimitedStreamMessageProducer(*owner));
        }
    }
    ~Data()
    {
        stopOrderedDispatcher();
        stopAsyncCompletionLoop();
        if (tp)
        {
            tp->stop();
            tp.reset();
        }
        delete message_producer;
    }
    uint8_t max_workers;
    std::atomic<int> m_id;
    std::shared_ptr<asio::thread_pool> tp;
    // Method calls may be cancelled by ID, so keep track of their state.
    // Async handlers may outlive the endpoint, so cancellation bookkeeping is
    // kept in a shared registry that retained async completions can release
    // safely after RemoteEndPoint destruction.
    std::shared_ptr<RequestCancellationRegistry> cancellation_registry {std::make_shared<RequestCancellationRegistry>()};
    std::shared_ptr<lsp::detail::AsyncResponseState> async_response_state {
        std::make_shared<lsp::detail::AsyncResponseState>()};

    std::atomic<unsigned> next_request_cookie; // To disambiguate reused IDs, see below.
    std::atomic<uint64_t> next_message_sequence {0};
    uint64_t nextMessageSequence()
    {
        return next_message_sequence.fetch_add(1, std::memory_order_relaxed);
    }
    void onCancel(Notify_Cancellation::notify* notify, uint64_t sequence)
    {
        std::lock_guard<std::mutex> lock(cancellation_registry->mutex);
        auto const it = cancellation_registry->requestCancelers.find(notify->params.id);
        if (it != cancellation_registry->requestCancelers.end())
        {
            it->second.first(); // Invoke the canceler.
            return;
        }
        auto pending = cancellation_registry->pendingCancelRequests.find(notify->params.id);
        if (pending == cancellation_registry->pendingCancelRequests.end() || pending->second < sequence)
        {
            cancellation_registry->pendingCancelRequests[notify->params.id] = sequence;
        }
    }

    // We run cancelable requests in a context that does two things:
    //  - allows cancellation using requestCancelers[ID]
    //  - cleans up the entry in requestCancelers when it's no longer needed
    // If a client reuses an ID, the last wins and the first cannot be canceled.
    Context cancelableRequestContext(lsRequestId id, uint64_t sequence)
    {
        auto task = cancelableTask(
            id,
            /*Reason=*/static_cast<int>(lsErrorCodes::RequestCancelled)
        );
        unsigned cookie;
        bool should_cancel = false;
        Canceler canceler_to_invoke;
        auto registry = cancellation_registry;
        {
            std::lock_guard<std::mutex> lock(registry->mutex);
            cookie = next_request_cookie.fetch_add(1, std::memory_order_relaxed);
            registry->requestCancelers[id] = {std::move(task.second), cookie};
            auto const pending = registry->pendingCancelRequests.find(id);
            if (pending != registry->pendingCancelRequests.end())
            {
                // If this id was already used, a pending cancel with an older
                // sequence belongs to a completed request and must not cancel
                // the reused id.
                should_cancel = registry->seenRequestIds.find(id) == registry->seenRequestIds.end()
                    || pending->second > sequence;
                registry->pendingCancelRequests.erase(pending);
            }
            registry->seenRequestIds.insert(id);
            if (should_cancel)
            {
                canceler_to_invoke = registry->requestCancelers[id].first;
            }
        }
        if (canceler_to_invoke)
        {
            canceler_to_invoke();
        }
        // When the request ends, we can clean up the entry we just added.
        // The cookie lets us check that it hasn't been overwritten due to ID
        // reuse.
        return task.first.derive(lsp::make_scope_exit(
            [registry, id, cookie]
            {
                std::lock_guard<std::mutex> lock(registry->mutex);
                RequestCancellationRegistry::CancelKey key {id, cookie};
                if (registry->retainedRequestCancelers.find(key) != registry->retainedRequestCancelers.end())
                {
                    return;
                }

                auto const& it = registry->requestCancelers.find(id);
                if (it != registry->requestCancelers.end() && it->second.second == cookie)
                {
                    registry->requestCancelers.erase(it);
                }
            }
        ));
    }

    std::shared_ptr<void> retainRequestCancellation(lsRequestId const& id)
    {
        auto registry = cancellation_registry;
        RequestCancellationRegistry::CancelKey key;
        {
            std::lock_guard<std::mutex> lock(registry->mutex);
            auto const it = registry->requestCancelers.find(id);
            if (it == registry->requestCancelers.end())
            {
                return {};
            }
            key = {id, it->second.second};
            ++registry->retainedRequestCancelers[key];
        }

        return std::shared_ptr<void>(
            new char(0),
            [registry, key](void* ptr)
            {
                delete static_cast<char*>(ptr);
                std::lock_guard<std::mutex> lock(registry->mutex);
                auto retained = registry->retainedRequestCancelers.find(key);
                if (retained != registry->retainedRequestCancelers.end())
                {
                    if (--retained->second != 0)
                    {
                        return;
                    }
                    registry->retainedRequestCancelers.erase(retained);
                }

                auto request = registry->requestCancelers.find(key.first);
                if (request != registry->requestCancelers.end() && request->second.second == key.second)
                {
                    registry->requestCancelers.erase(request);
                }
            }
        );
    }

    std::map<lsRequestId, std::shared_ptr<PendingRequestInfo>> _client_request_futures;
    StreamMessageProducer* message_producer;
    std::atomic<bool> quit {};
    RemoteEndPoint* owner;
    lsp::Log& log;
    std::shared_ptr<lsp::istream> input;
    std::shared_ptr<lsp::ostream> output;

    std::mutex m_requestInfo;
    std::mutex async_completion_mutex;
    std::condition_variable async_completion_cv;
    std::deque<std::function<bool()>> async_completions;
    std::thread async_completion_thread;
    bool async_completion_stop = false;

    struct ParkedRequest
    {
        uint64_t gate = 0;
        uint64_t sequence = 0;
        std::unique_ptr<LspMessage> message;
    };

    std::mutex ordered_mutex;
    std::condition_variable ordered_cv;
    std::deque<std::pair<uint64_t, std::unique_ptr<LspMessage>>> notification_queue;
    std::deque<ParkedRequest> parked_requests;
    std::thread notification_thread;
    bool notification_stop = false;
    bool has_last_ordered_notification = false;
    uint64_t last_ordered_notification_seq = 0;
    bool has_completed_notification = false;
    uint64_t completed_notification_seq = 0;

    bool notificationGateSatisfied(uint64_t gate) const
    {
        return has_completed_notification && completed_notification_seq >= gate;
    }

    void postToWorker(std::unique_ptr<LspMessage> msg, uint64_t sequence)
    {
        if (!msg || quit.load(std::memory_order_relaxed) || !tp)
        {
            return;
        }
        asio::post(
            *tp,
            [owner = owner, msg = std::move(msg), sequence]() mutable
            {
#ifdef LSPCPP_USEGC
                GCThreadContext gcContext;
#endif
                owner->mainLoopCatching(std::move(msg), sequence, nullptr);
            }
        );
    }

    void releaseParkedRequestsLocked(std::vector<ParkedRequest>& ready)
    {
        auto it = parked_requests.begin();
        while (it != parked_requests.end())
        {
            if (notificationGateSatisfied(it->gate))
            {
                ready.emplace_back(std::move(*it));
                it = parked_requests.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void postReadyParkedRequests(std::vector<ParkedRequest>& ready)
    {
        for (auto& request : ready)
        {
            postToWorker(std::move(request.message), request.sequence);
        }
        ready.clear();
    }

    void startOrderedDispatcher()
    {
        if (notification_thread.joinable() && notification_thread.get_id() != std::this_thread::get_id())
        {
            notification_thread.join();
        }
        std::lock_guard<std::mutex> lock(ordered_mutex);
        notification_stop = false;
        has_last_ordered_notification = false;
        last_ordered_notification_seq = 0;
        has_completed_notification = false;
        completed_notification_seq = 0;
        notification_queue.clear();
        parked_requests.clear();
        if (notification_thread.joinable())
        {
            return;
        }
        notification_thread = std::thread([this] { notificationLoop(); });
    }

    void stopOrderedDispatcher()
    {
        {
            std::lock_guard<std::mutex> lock(ordered_mutex);
            notification_stop = true;
            notification_queue.clear();
            parked_requests.clear();
        }
        ordered_cv.notify_all();
        if (notification_thread.joinable())
        {
            if (notification_thread.get_id() == std::this_thread::get_id())
            {
                return;
            }
            else
            {
                notification_thread.join();
            }
        }
    }

    void enqueueNotification(std::unique_ptr<LspMessage> msg, uint64_t sequence)
    {
        if (!msg || quit.load(std::memory_order_relaxed))
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(ordered_mutex);
            if (notification_stop || quit.load(std::memory_order_relaxed))
            {
                return;
            }
            has_last_ordered_notification = true;
            last_ordered_notification_seq = sequence;
            notification_queue.emplace_back(sequence, std::move(msg));
        }
        ordered_cv.notify_one();
    }

    void enqueueRequest(std::unique_ptr<LspMessage> msg, uint64_t sequence)
    {
        if (!msg || quit.load(std::memory_order_relaxed))
        {
            return;
        }

        bool post_now = true;
        {
            std::lock_guard<std::mutex> lock(ordered_mutex);
            if (!notification_stop && has_last_ordered_notification &&
                !notificationGateSatisfied(last_ordered_notification_seq))
            {
                ParkedRequest parked;
                parked.gate = last_ordered_notification_seq;
                parked.sequence = sequence;
                parked.message = std::move(msg);
                parked_requests.emplace_back(std::move(parked));
                post_now = false;
            }
        }

        if (post_now)
        {
            postToWorker(std::move(msg), sequence);
        }
    }

    void notificationLoop()
    {
        for (;;)
        {
            uint64_t sequence = 0;
            std::unique_ptr<LspMessage> msg;
            {
                std::unique_lock<std::mutex> lock(ordered_mutex);
                ordered_cv.wait(lock, [&] { return notification_stop || !notification_queue.empty(); });
                if (notification_stop && notification_queue.empty())
                {
                    return;
                }
                sequence = notification_queue.front().first;
                msg = std::move(notification_queue.front().second);
                notification_queue.pop_front();
            }

            try
            {
                owner->mainLoopCatching(std::move(msg), sequence, nullptr);
            }
            catch (...)
            {
            }

            std::vector<ParkedRequest> ready;
            {
                std::lock_guard<std::mutex> lock(ordered_mutex);
                has_completed_notification = true;
                completed_notification_seq = sequence;
                releaseParkedRequestsLocked(ready);
            }
            postReadyParkedRequests(ready);
        }
    }

    bool pendingRequest(RequestInMessage& info, PendingResponseHandler&& handler)
    {
        bool ret = true;
        std::lock_guard<std::mutex> lock(m_requestInfo);
        if (!info.id.has_value())
        {
            auto id = getNextRequestId();
            info.id.set(id);
        }
        else
        {
            if (_client_request_futures.find(info.id) != _client_request_futures.end())
            {
                return false;
            }
        }
        _client_request_futures[info.id] = std::make_shared<PendingRequestInfo>(info.method, handler);
        return ret;
    }
    std::shared_ptr<PendingRequestInfo> const getRequestInfo(lsRequestId const& _id)
    {
        std::lock_guard<std::mutex> lock(m_requestInfo);
        auto findIt = _client_request_futures.find(_id);
        if (findIt != _client_request_futures.end())
        {
            return findIt->second;
        }
        return nullptr;
    }

    void removeRequestInfo(lsRequestId const& _id)
    {
        std::lock_guard<std::mutex> lock(m_requestInfo);
        auto findIt = _client_request_futures.find(_id);
        if (findIt != _client_request_futures.end())
        {
            _client_request_futures.erase(findIt);
        }
    }
    void failPendingRequests()
    {
        std::vector<std::pair<lsRequestId, std::shared_ptr<PendingRequestInfo>>> pending;
        {
            std::lock_guard<std::mutex> lock(m_requestInfo);
            pending.reserve(_client_request_futures.size());
            for (auto& it : _client_request_futures)
            {
                pending.emplace_back(it.first, it.second);
            }
            _client_request_futures.clear();
        }

        for (auto& it : pending)
        {
            std::unique_ptr<LspMessage> msg(new Rsp_Error());
            auto error = static_cast<Rsp_Error*>(msg.get());
            error->id = it.first;
            error->error.code = lsErrorCodes::InternalError;
            error->error.message = "Remote endpoint stopped.";
            it.second->complete(msg);
        }
    }
    void asyncCompletionLoop()
    {
        // Number of consecutive completions that were still pending. Once it
        // reaches the queue size, a full pass made no progress and the loop
        // must back off instead of busy-polling the same unready futures.
        size_t pending_streak = 0;
        for (;;)
        {
            std::function<bool()> completion;
            {
                std::unique_lock<std::mutex> lock(async_completion_mutex);
                async_completion_cv.wait(
                    lock, [&] { return async_completion_stop || !async_completions.empty(); }
                );
                if (async_completion_stop)
                {
                    return;
                }
                if (pending_streak >= async_completions.size())
                {
                    size_t const size_before_wait = async_completions.size();
                    async_completion_cv.wait_for(
                        lock,
                        std::chrono::milliseconds(10),
                        [&] { return async_completion_stop || async_completions.size() > size_before_wait; }
                    );
                    if (async_completion_stop)
                    {
                        return;
                    }
                    pending_streak = 0;
                }
                completion = std::move(async_completions.front());
                async_completions.pop_front();
            }

            bool completed = true;
            try
            {
                completed = completion();
            }
            catch (...)
            {
                completed = true;
            }

            if (completed)
            {
                pending_streak = 0;
                continue;
            }

            std::lock_guard<std::mutex> lock(async_completion_mutex);
            if (async_completion_stop)
            {
                return;
            }
            async_completions.emplace_back(std::move(completion));
            ++pending_streak;
        }
    }
    void startAsyncCompletionLoop()
    {
        std::lock_guard<std::mutex> lock(async_completion_mutex);
        async_completion_stop = false;
        if (async_completion_thread.joinable())
        {
            return;
        }
        async_completion_thread = std::thread([this] { asyncCompletionLoop(); });
    }
    void stopAsyncCompletionLoop()
    {
        {
            std::lock_guard<std::mutex> lock(async_completion_mutex);
            async_completion_stop = true;
            async_completions.clear();
        }
        async_completion_cv.notify_all();
        if (async_completion_thread.joinable())
        {
            async_completion_thread.join();
        }
    }
    void postAsyncCompletion(std::function<bool()>&& completion)
    {
        {
            std::lock_guard<std::mutex> lock(async_completion_mutex);
            if (async_completion_stop || quit.load(std::memory_order_relaxed))
            {
                return;
            }
            async_completions.emplace_back(std::move(completion));
        }
        async_completion_cv.notify_one();
    }
    void clear()
    {
        quit.store(true, std::memory_order_relaxed);
        if (message_producer)
        {
            message_producer->keepRunning = false;
        }
        if (input)
        {
            input->interrupt();
        }
        if (async_response_state)
        {
            async_response_state->active.store(false, std::memory_order_relaxed);
            if (async_response_state->send_mutex)
            {
                std::lock_guard<std::mutex> lock(*async_response_state->send_mutex);
                async_response_state->output.reset();
            }
            else
            {
                async_response_state->output.reset();
            }
        }
        std::vector<Canceler> cancelers;
        {
            std::lock_guard<std::mutex> lock(cancellation_registry->mutex);
            for (auto& entry : cancellation_registry->requestCancelers)
            {
                cancelers.push_back(entry.second.first);
            }
            cancellation_registry->requestCancelers.clear();
            cancellation_registry->retainedRequestCancelers.clear();
            cancellation_registry->pendingCancelRequests.clear();
            cancellation_registry->seenRequestIds.clear();
        }
        for (auto& canceler : cancelers)
        {
            canceler();
        }
        failPendingRequests();
        stopOrderedDispatcher();
        stopAsyncCompletionLoop();
        if (tp)
        {
            tp->stop();
        }
    }

    int getNextRequestId()
    {
        return m_id.fetch_add(1, std::memory_order_relaxed);
    }
};

namespace
{
void WriterMsg(std::shared_ptr<lsp::ostream>& output, LspMessage& msg)
{
    auto const& s = msg.ToJson();
    auto const value = std::string("Content-Length: ") + std::to_string(s.size()) + "\r\n\r\n" + s;
    output->write(value);
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
} // namespace

CancelMonitor RemoteEndPoint::getCancelMonitor(lsRequestId const& id)
{
    auto monitor = getCancelledMonitor(id);
    if (monitor.has_value())
    {
        return monitor.value();
    }
    return [] { return 0; };
}

std::shared_ptr<lsp::detail::AsyncResponseState> RemoteEndPoint::getAsyncResponseState() const
{
    return d_ptr->async_response_state;
}

std::shared_ptr<void> RemoteEndPoint::retainRequestCancellation(lsRequestId const& id)
{
    return d_ptr->retainRequestCancellation(id);
}

void RemoteEndPoint::postAsyncCompletion(std::function<bool()>&& completion)
{
    d_ptr->postAsyncCompletion(std::move(completion));
}

void RemoteEndPoint::sendAsyncMessage(std::shared_ptr<lsp::detail::AsyncResponseState> const& state, LspMessage& msg)
{
    if (!state || !state->active.load(std::memory_order_relaxed) || !state->send_mutex)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(*state->send_mutex);
    if (!state->active.load(std::memory_order_relaxed) || !state->output || state->output->bad())
    {
        return;
    }
    WriterMsg(state->output, msg);
}

RemoteEndPoint::RemoteEndPoint(
    std::shared_ptr<MessageJsonHandler> const& json_handler, std::shared_ptr<Endpoint> const& localEndPoint,
    lsp::Log& _log, lsp::JSONStreamStyle style, uint8_t max_workers
)
    : d_ptr(new Data(style, max_workers, _log, this)), jsonHandler(json_handler), local_endpoint(localEndPoint)
{
    jsonHandler->method2notification[Notify_Cancellation::notify::kMethodInfo] = [](Reader& visitor)
    { return Notify_Cancellation::notify::ReflectReader(visitor); };
    d_ptr->async_response_state->send_mutex = m_sendMutex;

    d_ptr->quit.store(false, std::memory_order_relaxed);
}

RemoteEndPoint::~RemoteEndPoint()
{
    stop();
    delete d_ptr;
    d_ptr = nullptr;
}

RemoteEndPoint::ParsedMessage RemoteEndPoint::parseAndClassify(std::string const& content)
{
    ParsedMessage result;
    rapidjson::Document document;
    document.Parse(content.c_str(), content.length());
    if (document.HasParseError())
    {
        std::string info = "lsp msg format error:";
        rapidjson::GetParseErrorFunc GetParseError = rapidjson::GetParseError_En; // or whatever
        info += GetParseError(document.GetParseError());
        info += "\n";
        info += "ErrorContext offset:\n";
        info += content.substr(document.GetErrorOffset());
        d_ptr->log.log(Log::Level::SEVERE, info);

        return result;
    }

    JsonReader visitor {&document};
    if (!visitor.HasMember("jsonrpc") || !visitor["jsonrpc"]->IsString() ||
        std::string(visitor["jsonrpc"]->GetString()) != "2.0")
    {
        std::string reason;
        reason = "Reason:Bad or missing jsonrpc version\n";
        reason += "content:\n" + content;
        d_ptr->log.log(Log::Level::SEVERE, reason);
        return result;
    }
    LspMessage::Kind _kind = LspMessage::NOTIFICATION_MESSAGE;
    try
    {
        if (isRequestMessage(visitor))
        {
            _kind = LspMessage::REQUEST_MESSAGE;
            auto msg = jsonHandler->parseRequstMessage(visitor["method"]->GetString(), visitor);
            if (msg)
            {
                result.ok = true;
                result.kind = LspMessage::REQUEST_MESSAGE;
                result.message = std::move(msg);
                return result;
            }
            else
            {
                std::string info = "Unknown support request message when consumer message:\n";
                info += content;
                d_ptr->log.log(Log::Level::WARNING, info);
                return result;
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
                std::string info = "Unknown response message :\n";
                info += content;
                d_ptr->log.log(Log::Level::INFO, info);
                result.ok = true;
                result.kind = LspMessage::RESPONCE_MESSAGE;
                return result;
            }
            else
            {

                auto msg = jsonHandler->parseResponseMessage(msgInfo->method, visitor);
                if (msg)
                {
                    result.ok = true;
                    result.kind = LspMessage::RESPONCE_MESSAGE;
                    result.message = std::move(msg);
                    return result;
                }
                else
                {
                    std::string info = "Unknown response message :\n";
                    info += content;
                    d_ptr->log.log(Log::Level::SEVERE, info);
                    return result;
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
                return result;
            }
            result.ok = true;
            result.kind = LspMessage::NOTIFICATION_MESSAGE;
            result.message = std::move(msg);
            return result;
        }
        else
        {
            std::string info = "Unknown lsp message when consumer message:\n";
            info += content;
            d_ptr->log.log(Log::Level::WARNING, info);
            return result;
        }
    }
    catch (std::exception& e)
    {

        std::string info = "Exception  when process ";
        if (_kind == LspMessage::REQUEST_MESSAGE)
        {
            info += "request";
        }
        else if (_kind == LspMessage::RESPONCE_MESSAGE)
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
        return result;
    }
    return result;
}

void RemoteEndPoint::mainLoopCatching(std::unique_ptr<LspMessage> msg, uint64_t sequence, std::string const* content)
{
    if (!msg)
    {
        return;
    }
    auto const kind = msg->GetKid();
    try
    {
        mainLoop(std::move(msg), sequence);
    }
    catch (std::exception& e)
    {
        std::string info = "Exception  when process ";
        if (kind == LspMessage::REQUEST_MESSAGE)
        {
            info += "request";
        }
        else if (kind == LspMessage::RESPONCE_MESSAGE)
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
        if (content != nullptr)
        {
            reason += "content:\n" + *content;
        }
        d_ptr->log.log(Log::Level::SEVERE, reason);
    }
}

bool RemoteEndPoint::dispatch(std::string const& content, uint64_t sequence)
{
    ParsedMessage parsed = parseAndClassify(content);
    if (parsed.ok && parsed.message)
    {
        mainLoopCatching(std::move(parsed.message), sequence, &content);
    }
    return parsed.ok;
}

void RemoteEndPoint::routeIncoming(std::string&& content, uint64_t sequence)
{
    ParsedMessage parsed = parseAndClassify(content);
    if (!parsed.ok || !parsed.message || d_ptr->quit.load(std::memory_order_relaxed))
    {
        return;
    }

    if (parsed.kind == LspMessage::NOTIFICATION_MESSAGE)
    {
        if (strcmp(Notify_Cancellation::notify::kMethodInfo, parsed.message->GetMethodType()) == 0)
        {
            d_ptr->onCancel(static_cast<Notify_Cancellation::notify*>(parsed.message.get()), sequence);
        }
        else
        {
            d_ptr->enqueueNotification(std::move(parsed.message), sequence);
        }
    }
    else if (parsed.kind == LspMessage::REQUEST_MESSAGE)
    {
        d_ptr->enqueueRequest(std::move(parsed.message), sequence);
    }
    else
    {
        d_ptr->postToWorker(std::move(parsed.message), sequence);
    }
}

bool RemoteEndPoint::internalSendRequest(RequestInMessage& info, GenericResponseHandler handler)
{
    std::lock_guard<std::mutex> lock(*m_sendMutex);
    if (!d_ptr->output || d_ptr->output->bad())
    {
        std::string desc = "Output isn't good any more:\n";
        d_ptr->log.log(Log::Level::WARNING, desc);
        return false;
    }
    PendingResponseHandler pending_handler = [handler = std::move(handler)](std::unique_ptr<LspMessage>& msg) mutable
    { return handler(std::move(msg)); };
    if (!d_ptr->pendingRequest(info, std::move(pending_handler)))
    {
        std::string desc = "Duplicate id  which of request:";
        desc += info.ToJson();
        desc += "\n";
        d_ptr->log.log(Log::Level::WARNING, desc);
        return false;
    }
    WriterMsg(d_ptr->output, info);
    return true;
}

int RemoteEndPoint::getNextRequestId()
{
    return d_ptr->getNextRequestId();
}
bool RemoteEndPoint::cancelRequest(lsRequestId const& id)
{
    if (!isWorking())
    {
        return false;
    }
    auto msgInfo = d_ptr->getRequestInfo(id);
    if (msgInfo)
    {
        Notify_Cancellation::notify cancel_notify;
        cancel_notify.params.id = id;
        send(cancel_notify);
        return true;
    }
    return false;
}

void RemoteEndPoint::removeRequestInfo(lsRequestId const& id)
{
    d_ptr->removeRequestInfo(id);
}

std::unique_ptr<LspMessage> RemoteEndPoint::internalWaitResponse(RequestInMessage& request, unsigned time_out)
{
    auto eventFuture = std::make_shared<Condition<LspMessage>>();
    if (!internalSendRequest(
        request,
        [=](std::unique_ptr<LspMessage> data)
        {
            eventFuture->notify(std::move(data));
            return true;
        }
    ))
    {
        return {};
    }
    auto response = eventFuture->wait(time_out);
    if (!response)
    {
        d_ptr->removeRequestInfo(request.id);
    }
    return response;
}

void RemoteEndPoint::mainLoop(std::unique_ptr<LspMessage> msg, uint64_t sequence)
{
    if (d_ptr->quit.load(std::memory_order_relaxed))
    {
        return;
    }
    auto const _kind = msg->GetKid();
    if (_kind == LspMessage::REQUEST_MESSAGE)
    {
        auto req = static_cast<RequestInMessage*>(msg.get());
        auto const request_id = req->id;
        auto const method = std::string(req->GetMethodType());
        // Calls can be canceled by the client. Add cancellation context.
        WithContext WithCancel(d_ptr->cancelableRequestContext(request_id, sequence));
        if (!local_endpoint->onRequest(std::move(msg)))
        {
            Rsp_Error error;
            error.id = request_id;
            error.error.code = lsErrorCodes::MethodNotFound;
            error.error.message = "Method not found: " + method;
            send(error);
        }
    }

    else if (_kind == LspMessage::RESPONCE_MESSAGE)
    {
        auto const id = static_cast<ResponseInMessage*>(msg.get())->id;
        auto msgInfo = d_ptr->getRequestInfo(id);
        if (!msgInfo)
        {
            auto const _method_desc = msg->GetMethodType();
            local_endpoint->onResponse(_method_desc, std::move(msg));
        }
        else
        {
            bool needLocal = true;
            if (msgInfo->complete(msg))
            {
                needLocal = false;
            }
            if (needLocal && msg)
            {
                local_endpoint->onResponse(msgInfo->method, std::move(msg));
            }
            d_ptr->removeRequestInfo(id);
        }
    }
    else if (_kind == LspMessage::NOTIFICATION_MESSAGE)
    {
        if (strcmp(Notify_Cancellation::notify::kMethodInfo, msg->GetMethodType()) == 0)
        {
            d_ptr->onCancel(static_cast<Notify_Cancellation::notify*>(msg.get()), sequence);
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
    for (auto& it : issue)
    {
        d_ptr->log.log(it.code, it.text);
    }
}

void RemoteEndPoint::handle(MessageIssue&& issue)
{
    d_ptr->log.log(issue.code, issue.text);
}

void RemoteEndPoint::startProcessingMessages(std::shared_ptr<lsp::istream> r, std::shared_ptr<lsp::ostream> w)
{
    d_ptr->quit.store(false, std::memory_order_relaxed);
    d_ptr->input = r;
    d_ptr->output = w;
    d_ptr->async_response_state = std::make_shared<lsp::detail::AsyncResponseState>();
    d_ptr->async_response_state->output = w;
    d_ptr->async_response_state->send_mutex = m_sendMutex;
    d_ptr->startAsyncCompletionLoop();
    d_ptr->message_producer->bind(r);
    d_ptr->tp = std::make_shared<asio::thread_pool>(d_ptr->max_workers);
    d_ptr->startOrderedDispatcher();
    message_producer_thread_ = std::make_shared<std::thread>(
        [&]()
        {
            d_ptr->message_producer->listen(
                [&](std::string&& content)
                {
                    auto const sequence = d_ptr->nextMessageSequence();
                    routeIncoming(std::move(content), sequence);
                }
            );
        }
    );
}

void RemoteEndPoint::stop()
{
    if (!d_ptr)
    {
        return;
    }
    d_ptr->clear();
    if (message_producer_thread_ && message_producer_thread_->joinable())
    {
        if (message_producer_thread_->get_id() == std::this_thread::get_id())
        {
            message_producer_thread_->detach();
        }
        else
        {
            message_producer_thread_->join();
        }
    }
    message_producer_thread_ = nullptr;
}

void RemoteEndPoint::sendMsg(LspMessage& msg)
{

    std::lock_guard<std::mutex> lock(*m_sendMutex);
    if (!d_ptr->output || d_ptr->output->bad())
    {
        std::string info = "Output isn't good any more:\n";
        d_ptr->log.log(Log::Level::INFO, info);
        return;
    }
    WriterMsg(d_ptr->output, msg);
}

bool RemoteEndPoint::isWorking() const
{
    if (message_producer_thread_ && message_producer_thread_->joinable())
    {
        return true;
    }
    return false;
}
