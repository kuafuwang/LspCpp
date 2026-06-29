#include "LibLsp/JsonRpc/Context.h"
#include "LibLsp/JsonRpc/ScopeExit.h"
#include "LibLsp/JsonRpc/lsRequestId.h"
#include "LibLsp/JsonRpc/threaded_queue.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace
{
using test::Expect;

void TestEnqueueAndDequeueBasic()
{
    ThreadedQueue<int> queue;
    queue.Enqueue(1, false);
    queue.Enqueue(2, false);
    queue.Enqueue(3, false);

    Expect(queue.Dequeue() == 1, "queue must dequeue first item first");
    Expect(queue.Dequeue() == 2, "queue must dequeue second item second");
    Expect(queue.Dequeue() == 3, "queue must dequeue third item third");
    Expect(queue.IsEmpty(), "queue must be empty after dequeuing all items");
}

void TestTryDequeueOnEmptyReturnsNullopt()
{
    ThreadedQueue<int> queue;

    auto item = queue.TryDequeue(false);

    Expect(!item.has_value(), "TryDequeue on an empty queue must return nullopt");
}

void TestEnqueueAllAndTryDequeueSome()
{
    ThreadedQueue<int> queue;
    std::vector<int> input {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    queue.EnqueueAll(std::move(input), false);

    auto first_half = queue.TryDequeueSome(5);

    Expect(first_half.size() == 5, "TryDequeueSome must return requested count when available");
    Expect(queue.Size() == 5, "TryDequeueSome must leave the remaining items queued");
    for (int i = 0; i < 5; ++i)
    {
        Expect(first_half[static_cast<size_t>(i)] == i, "TryDequeueSome must preserve FIFO order");
    }
}

void TestDequeueAllReturnsEverything()
{
    ThreadedQueue<int> queue;
    std::vector<int> input {1, 2, 3, 4, 5};
    queue.EnqueueAll(std::move(input), false);

    auto all = queue.DequeueAll();

    Expect(all.size() == 5, "DequeueAll must return all queued items");
    Expect(queue.IsEmpty(), "DequeueAll must empty the queue");
}

void TestMultiQueueWaiterWakesOnEnqueue()
{
    auto waiter = std::make_shared<MultiQueueWaiter>();
    ThreadedQueue<int> queue(waiter);
    std::atomic<bool> quit {false};

    std::thread producer(
        [&]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            queue.Enqueue(7, false);
        });

    bool const interrupted = waiter->Wait(quit, &queue);
    producer.join();

    Expect(!interrupted, "MultiQueueWaiter must wake on enqueue without reporting interrupt");
    Expect(queue.TryDequeue(false).value() == 7, "queued value must be available after waiter wakes");
}

void TestMultiQueueWaiterInterruptReturnsTrue()
{
    auto waiter = std::make_shared<MultiQueueWaiter>();
    ThreadedQueue<int> queue(waiter);
    std::atomic<bool> quit {false};

    std::thread interrupter(
        [&]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            quit.store(true, std::memory_order_relaxed);
            waiter->cv.notify_all();
        });

    bool const interrupted = waiter->Wait(quit, &queue);
    interrupter.join();

    Expect(interrupted, "MultiQueueWaiter must report interrupt when quit becomes true");
}

void TestContextDeriveAndGet()
{
    static lsp::Key<int> key;
    auto root = lsp::Context::empty();
    auto child = root.derive(key, 42);

    Expect(root.get(key) == nullptr, "parent context must not contain child value");
    Expect(child.get(key) != nullptr, "derived context must contain value");
    if (child.get(key))
    {
        Expect(*child.get(key) == 42, "derived context value mismatch");
    }
}

void TestWithContextRestoresOnDestruction()
{
    static lsp::Key<int> key;
    auto original = lsp::Context::current().clone();
    {
        lsp::WithContext with_context(lsp::Context::current().derive(key, 99));
        auto const* value = lsp::Context::current().get(key);
        Expect(value != nullptr && *value == 99, "WithContext must install derived context");
    }

    Expect(
        lsp::Context::current().get(key) == original.get(key),
        "WithContext must restore previous context on destruction");
}

void TestScopeExitRunsOnDestruction()
{
    bool ran = false;
    {
        auto cleanup = lsp::make_scope_exit([&]() { ran = true; });
        (void)cleanup;
    }

    Expect(ran, "scope_exit must run at scope exit");
}

void TestScopeExitReleasePreventsExecution()
{
    bool ran = false;
    {
        auto cleanup = lsp::make_scope_exit([&]() { ran = true; });
        (void)cleanup;
        cleanup.release();
    }

    Expect(!ran, "scope_exit release must prevent execution");
}

void TestToStringIntId()
{
    lsRequestId id;
    id.set(123);

    Expect(ToString(id) == "123", "integer request id ToString mismatch");
}

void TestToStringStringId()
{
    lsRequestId id;
    id.set("abc");

    Expect(ToString(id) == "abc", "string request id ToString mismatch");
}

void TestToStringNoneId()
{
    lsRequestId id;

    Expect(ToString(id).empty(), "empty request id ToString must be empty");
}
} // namespace

int main()
{
    TestEnqueueAndDequeueBasic();
    TestTryDequeueOnEmptyReturnsNullopt();
    TestEnqueueAllAndTryDequeueSome();
    TestDequeueAllReturnsEverything();
    TestMultiQueueWaiterWakesOnEnqueue();
    TestMultiQueueWaiterInterruptReturnsTrue();
    TestContextDeriveAndGet();
    TestWithContextRestoresOnDestruction();
    TestScopeExitRunsOnDestruction();
    TestScopeExitReleasePreventsExecution();
    TestToStringIntId();
    TestToStringStringId();
    TestToStringNoneId();

    return test::Failures() == 0 ? 0 : 1;
}
