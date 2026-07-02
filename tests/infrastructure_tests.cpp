#include "LibLsp/JsonRpc/Context.h"
#include "LibLsp/LspCpp.h"
#include "LibLsp/JsonRpc/ScopeExit.h"
#include "LibLsp/JsonRpc/lsRequestId.h"
#include "LibLsp/JsonRpc/threaded_queue.h"
#include "LibLsp/lsp/utils.h"
#include "LibLsp/lsp/working_files.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace lsp
{
lsPosition GetPositionForOffset(size_t offset, std::string const& content);
}

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

void TestPriorityQueueOrdering()
{
    ThreadedQueue<int> queue;
    queue.Enqueue(1, false);
    queue.Enqueue(10, true);
    queue.Enqueue(2, false);
    queue.Enqueue(11, true);

    Expect(queue.Dequeue() == 10, "priority queue items must be dequeued before normal items");
    Expect(queue.Dequeue() == 11, "priority queue must preserve FIFO order within priority items");
    Expect(queue.Dequeue() == 1, "normal queue items must follow priority items");
    Expect(queue.Dequeue() == 2, "normal queue must preserve FIFO order after priority drain");
}

void TestTryDequeuePreferenceAndPriorityBoundary()
{
    ThreadedQueue<int> queue;
    queue.Enqueue(1, false);
    queue.Enqueue(10, true);
    queue.Enqueue(2, false);

    auto normal_first = queue.TryDequeue(false);
    Expect(normal_first && *normal_first == 1, "TryDequeue(false) must prefer normal queue items");

    auto remaining = queue.TryDequeueSome(10);
    Expect(remaining.size() == 2, "TryDequeueSome must return all remaining items when count is larger than size");
    Expect(remaining[0] == 10, "TryDequeueSome must drain priority items before normal items");
    Expect(remaining[1] == 2, "TryDequeueSome must drain normal items after priority items");
    Expect(queue.IsEmpty(), "TryDequeueSome must empty the queue after draining all items");
}

void TestBlockingDequeueWaitsForProducer()
{
    ThreadedQueue<int> queue;
    std::atomic<bool> consumer_started {false};
    int result = 0;

    std::thread consumer(
        [&]()
        {
            consumer_started.store(true, std::memory_order_relaxed);
            result = queue.Dequeue();
        });

    while (!consumer_started.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue.Enqueue(42, false);
    consumer.join();

    Expect(result == 42, "blocking Dequeue must wait until a producer enqueues an item");
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

void TestMultiQueueWaiterWakesOnSecondQueue()
{
    auto waiter = std::make_shared<MultiQueueWaiter>();
    ThreadedQueue<int> first(waiter);
    ThreadedQueue<int> second(waiter);
    std::atomic<bool> quit {false};

    std::thread producer(
        [&]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            second.Enqueue(99, false);
        });

    bool const interrupted = waiter->Wait(quit, &first, &second);
    producer.join();

    Expect(!interrupted, "MultiQueueWaiter must wake when any watched queue receives state");
    Expect(first.IsEmpty(), "first watched queue must remain empty");
    auto value = second.TryDequeue(false);
    Expect(value && *value == 99, "second watched queue must hold the produced value");
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

void TestPublicLogAndStreamHelpers()
{
    lsp::NullLog null_log;
    null_log.info("ignored");

    std::istringstream input("abc");
    std::ostringstream output;
    auto input_stream = lsp::make_istream(input);
    auto output_stream = lsp::make_ostream(output);

    char buffer[3] = {};
    input_stream->read(buffer, 3);
    output_stream->write(std::string(buffer, 3)).flush();

    Expect(output.str() == "abc", "public stdio stream helpers must wrap standard streams");
}

void TestStderrLogWritesMessages()
{
    std::ostringstream narrow_output;
    std::wostringstream wide_output;
    auto* old_cerr = std::cerr.rdbuf(narrow_output.rdbuf());
    auto* old_wcerr = std::wcerr.rdbuf(wide_output.rdbuf());
    {
        auto restore = lsp::make_scope_exit(
            [&]()
            {
                std::cerr.rdbuf(old_cerr);
                std::wcerr.rdbuf(old_wcerr);
            });

        lsp::StderrLog log;
        log.error("narrow message");
        log.warning(L"wide message");
    }

    Expect(
        narrow_output.str().find("narrow message") != std::string::npos,
        "StderrLog must write narrow messages to std::cerr");
    Expect(
        wide_output.str().find(L"wide message") != std::wstring::npos,
        "StderrLog must write wide messages to std::wcerr");
}

void TestLanguageSessionFacadeKeepsEndpointAccessible()
{
    lsp::NullLog log;
    lsp::LanguageSession session(log);

    bool initialized = false;
    session.on(
        [&](td_initialize::request const& req)
        {
            initialized = true;
            td_initialize::response rsp;
            rsp.id = req.id;
            return rsp;
        });

    Expect(session.protocolJsonHandler() != nullptr, "LanguageSession must expose its protocol handler");
    Expect(session.localEndpoint() != nullptr, "LanguageSession must expose its local endpoint");
    session.endpoint().getNextRequestId();
    Expect(
        session.protocolJsonHandler()->GetRequestJsonHandler(td_initialize::request::kMethodInfo) != nullptr,
        "LanguageSession must register handlers on the underlying protocol handler");
    Expect(!initialized, "registering a LanguageSession handler must not invoke it immediately");
}

void TestWorkingFilesRangeChangeUsesCachedLineOffsets()
{
    WorkingFiles files;
    AbsolutePath path("/tmp/lspcpp-working-files-test.cpp", false);

    lsTextDocumentItem open;
    open.uri = lsDocumentUri(path);
    open.languageId = "cpp";
    open.version = 1;
    open.text = "alpha\nbeta\ngamma\n";
    auto file = files.OnOpen(open);

    lsTextDocumentDidChangeParams change;
    change.textDocument.uri = lsDocumentUri(path);
    change.textDocument.version = optional<int>(2);

    lsTextDocumentContentChangeEvent edit;
    edit.range = lsRange(lsPosition(1, 1), lsPosition(1, 3));
    edit.text = "EE";
    change.contentChanges.push_back(edit);
    files.OnChange(change);

    std::string content;
    bool const got_content = files.GetFileBufferContent(file, content);
    Expect(got_content, "WorkingFiles must return content for an open file");
    Expect(content == "alpha\nbEEa\ngamma\n", "WorkingFiles ranged change must preserve old API behavior");
}

void TestWorkingFilesMaintainsLineOffsetIndex()
{
    // Uses a large file to verify the cached line-offset index is built and
    // rebuilt after both ranged and full-content changes.
    WorkingFiles files;
    AbsolutePath path("/tmp/lspcpp-working-files-large-test.cpp", false);

    std::string text;
    for (int i = 0; i < 10000; ++i)
    {
        text += "line ";
        text += std::to_string(i);
        text += "\n";
    }

    lsTextDocumentItem open;
    open.uri = lsDocumentUri(path);
    open.languageId = "cpp";
    open.version = 1;
    open.text = text;
    auto file = files.OnOpen(open);

    Expect(file->LineOffsetCountForTest() == 10001, "WorkingFiles must build one cached line offset per line start");

    lsTextDocumentDidChangeParams tail_change;
    tail_change.textDocument.uri = lsDocumentUri(path);
    tail_change.textDocument.version = optional<int>(2);

    lsTextDocumentContentChangeEvent tail_edit;
    tail_edit.range = lsRange(lsPosition(9999, 5), lsPosition(9999, 9));
    tail_edit.text = "tail";
    tail_change.contentChanges.push_back(tail_edit);
    files.OnChange(tail_change);

    Expect(
        file->LineOffsetCountForTest() == 10001,
        "WorkingFiles must keep cached line offsets rebuilt after ranged changes");

    lsTextDocumentDidChangeParams full_change;
    full_change.textDocument.uri = lsDocumentUri(path);
    full_change.textDocument.version = optional<int>(3);

    lsTextDocumentContentChangeEvent full_edit;
    full_edit.text = "one\ntwo\n";
    full_change.contentChanges.push_back(full_edit);
    files.OnChange(full_change);

    Expect(file->LineOffsetCountForTest() == 3, "WorkingFiles must rebuild line offsets after full-content changes");
}

void TestAsciiPositionOffsetRoundTrip()
{
    std::string const content = "abc\ndef\nlast";
    for (int offset = 0; offset <= static_cast<int>(content.size()); ++offset)
    {
        lsPosition const position = lsp::GetPositionForOffset(static_cast<size_t>(offset), content);
        int const round_trip = lsp::GetOffsetForPosition(position, content);
        Expect(round_trip == offset, "ASCII position/offset conversion must round-trip every byte offset");
    }
}

void TestStringAndPathUtilities()
{
    Expect(lsp::StartsWith("alpha.cpp", "alpha"), "StartsWith must detect prefixes");
    Expect(lsp::EndsWith("alpha.cpp", ".cpp"), "EndsWith must detect suffixes");
    Expect(lsp::GetDirName("/tmp/lspcpp/file.cpp") == "/tmp/lspcpp/", "GetDirName must keep trailing slash");
    Expect(lsp::GetBaseName("/tmp/lspcpp/file.cpp") == "file.cpp", "GetBaseName must return final path component");
    Expect(lsp::StripFileType("/tmp/lspcpp/file.cpp") == "/tmp/lspcpp/file", "StripFileType must remove suffix");
    Expect(
        lsp::ReplaceAll("a-b-c", "-", "::") == "a::b::c",
        "ReplaceAll must replace every occurrence of the source token");
}
} // namespace

int main()
{
    TestEnqueueAndDequeueBasic();
    TestTryDequeueOnEmptyReturnsNullopt();
    TestEnqueueAllAndTryDequeueSome();
    TestDequeueAllReturnsEverything();
    TestPriorityQueueOrdering();
    TestTryDequeuePreferenceAndPriorityBoundary();
    TestBlockingDequeueWaitsForProducer();
    TestMultiQueueWaiterWakesOnEnqueue();
    TestMultiQueueWaiterInterruptReturnsTrue();
    TestMultiQueueWaiterWakesOnSecondQueue();
    TestContextDeriveAndGet();
    TestWithContextRestoresOnDestruction();
    TestScopeExitRunsOnDestruction();
    TestScopeExitReleasePreventsExecution();
    TestToStringIntId();
    TestToStringStringId();
    TestToStringNoneId();
    TestPublicLogAndStreamHelpers();
    TestStderrLogWritesMessages();
    TestLanguageSessionFacadeKeepsEndpointAccessible();
    TestWorkingFilesRangeChangeUsesCachedLineOffsets();
    TestWorkingFilesMaintainsLineOffsetIndex();
    TestAsciiPositionOffsetRoundTrip();
    TestStringAndPathUtilities();

    return test::Failures() == 0 ? 0 : 1;
}
