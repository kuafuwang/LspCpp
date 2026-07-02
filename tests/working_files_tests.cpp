#include "LibLsp/lsp/working_files.h"
#include "LibLsp/lsp/utils.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace
{
using test::Expect;

AbsolutePath MakePath(std::string const& name)
{
    return AbsolutePath("/tmp/lspcpp-working-files-suite/" + name, false);
}

std::shared_ptr<WorkingFile> OpenFile(WorkingFiles& files, AbsolutePath const& path, std::string text, int version = 1)
{
    lsTextDocumentItem open;
    open.uri = lsDocumentUri(path);
    open.languageId = "cpp";
    open.version = version;
    open.text = std::move(text);
    return files.OnOpen(open);
}

lsTextDocumentContentChangeEvent RangedEdit(lsPosition start, lsPosition end, std::string text)
{
    lsTextDocumentContentChangeEvent edit;
    edit.range = lsRange(start, end);
    edit.text = std::move(text);
    return edit;
}

std::string ContentOf(WorkingFiles& files, std::shared_ptr<WorkingFile>& file)
{
    std::string content;
    Expect(files.GetFileBufferContent(file, content), "expected open file content to be available");
    return content;
}

void TestOffsetBoundariesAndLineEndings()
{
    WorkingFiles files;
    auto file = OpenFile(files, MakePath("offsets.cpp"), "a\r\nbb\nccc");

    Expect(file->GetOffsetForPosition(lsPosition(0, 0)) == 0, "offset for file start must be zero");
    Expect(file->GetOffsetForPosition(lsPosition(0, 2)) == 2, "CRLF line should count carriage return as a character");
    Expect(file->GetOffsetForPosition(lsPosition(1, 1)) == 4, "offset for second line must use cached line start");
    Expect(file->GetOffsetForPosition(lsPosition(2, 3)) == 9, "offset at end of last line mismatch");
    Expect(
        file->GetOffsetForPosition(lsPosition(20, 0)) == static_cast<int>(file->GetContentNoLock().size()),
        "out-of-range line must clamp to end of document");
    Expect(
        file->GetOffsetForPosition(lsPosition(2, 99)) == static_cast<int>(file->GetContentNoLock().size()),
        "out-of-range character must clamp to end of document");
}

void TestUtf8OffsetsUseCodePointCounting()
{
    WorkingFiles files;
    auto file = OpenFile(files, MakePath("utf8.cpp"), "a\xc3\xa9\nb\xf0\x9f\x98\x80z");

    Expect(file->GetOffsetForPosition(lsPosition(0, 1)) == 1, "ASCII prefix offset mismatch");
    Expect(file->GetOffsetForPosition(lsPosition(0, 2)) == 3, "UTF-8 two-byte code point must advance as one character");
    Expect(file->GetOffsetForPosition(lsPosition(1, 2)) == 9, "UTF-8 four-byte code point must advance as one character");
}

void TestMultipleEditsRebuildLineOffsetsSequentially()
{
    WorkingFiles files;
    AbsolutePath const path = MakePath("multi-edit.cpp");
    auto file = OpenFile(files, path, "one\ntwo\nthree\n");

    lsTextDocumentDidChangeParams change;
    change.textDocument.uri = lsDocumentUri(path);
    change.textDocument.version = optional<int>(2);
    change.contentChanges.push_back(RangedEdit(lsPosition(0, 0), lsPosition(0, 0), "zero\n"));
    change.contentChanges.push_back(RangedEdit(lsPosition(2, 0), lsPosition(3, 0), "TWO-AND-"));
    change.contentChanges.push_back(RangedEdit(lsPosition(2, 8), lsPosition(2, 13), "THREE"));

    files.OnChange(change);

    Expect(ContentOf(files, file) == "zero\none\nTWO-AND-THREE\n", "multiple ranged edits must apply sequentially");
    Expect(file->version == 2, "WorkingFiles must update version from didChange");
    Expect(file->LineOffsetCountForTest() == 4, "line offsets must be rebuilt after multi-edit change");
}

void TestFullReplaceAndDocumentBoundaryEdits()
{
    WorkingFiles files;
    AbsolutePath const path = MakePath("full-replace.cpp");
    auto file = OpenFile(files, path, "middle");

    lsTextDocumentDidChangeParams full_change;
    full_change.textDocument.uri = lsDocumentUri(path);
    full_change.textDocument.version = optional<int>(2);
    lsTextDocumentContentChangeEvent full_edit;
    full_edit.text = "alpha\nomega";
    full_change.contentChanges.push_back(full_edit);
    files.OnChange(full_change);
    Expect(ContentOf(files, file) == "alpha\nomega", "full document replace must replace all content");

    lsTextDocumentDidChangeParams boundary_change;
    boundary_change.textDocument.uri = lsDocumentUri(path);
    boundary_change.textDocument.version = optional<int>(3);
    boundary_change.contentChanges.push_back(RangedEdit(lsPosition(0, 0), lsPosition(0, 0), "BEGIN\n"));
    boundary_change.contentChanges.push_back(RangedEdit(lsPosition(2, 5), lsPosition(2, 5), "\nEND"));
    files.OnChange(boundary_change);

    Expect(
        ContentOf(files, file) == "BEGIN\nalpha\nomega\nEND",
        "boundary inserts must work at document start and end");
    Expect(file->LineOffsetCountForTest() == 4, "line offsets must include every line start after boundary edits");
}

void TestLifecycleOpenCloseClearAndDirectories()
{
    WorkingFiles files;
    AbsolutePath const first = MakePath("lifecycle/a.cpp");
    AbsolutePath const second = MakePath("lifecycle/b.cpp");
    auto first_file = OpenFile(files, first, "old", 1);
    auto reopened = OpenFile(files, first, "new", 7);
    auto second_file = OpenFile(files, second, "other", 1);

    Expect(first_file == reopened, "reopening the same URI must reuse the existing WorkingFile");
    Expect(reopened->version == 7, "reopening must update file version");
    Expect(ContentOf(files, reopened) == "new", "reopening must replace file content");

    std::wstring wide_content;
    Expect(files.GetFileBufferContent(reopened, wide_content), "wstring content lookup must succeed");
    Expect(wide_content == L"new", "wstring content lookup must convert current buffer");

    lsTextDocumentIdentifier close;
    close.uri = lsDocumentUri(first);
    Expect(files.OnClose(close), "OnClose must remove an open file");
    std::string closed_content;
    Expect(!files.GetFileBufferContent(first, closed_content), "closed file lookup by path must fail");
    Expect(!files.OnClose(close), "OnClose must report false for already-closed file");

    files.CloseFilesInDirectory({Directory(AbsolutePath("/tmp/lspcpp-working-files-suite/lifecycle/", false))});
    Expect(!files.GetFileBufferContent(second, closed_content), "CloseFilesInDirectory must remove matching files");

    AbsolutePath const clear_path = MakePath("clear.cpp");
    OpenFile(files, clear_path, "present");
    files.Clear();
    Expect(!files.GetFileBufferContent(clear_path, closed_content), "Clear must remove all tracked files");
}

void TestOnSaveWritesCurrentContent()
{
    WorkingFiles files;
    AbsolutePath const path("/tmp/lspcpp-working-files-save-test.txt", false);
    std::remove(path.path.c_str());
    auto file = OpenFile(files, path, "saved content");

    lsTextDocumentIdentifier save;
    save.uri = lsDocumentUri(path);
    auto saved = files.OnSave(save);

    Expect(saved == file, "OnSave must return the saved WorkingFile");
    auto disk_content = lsp::ReadContent(path);
    Expect(disk_content.has_value(), "OnSave must create the target file");
    if (disk_content)
    {
        Expect(*disk_content == "saved content", "OnSave must write the current buffer content");
    }
    std::remove(path.path.c_str());
}

void TestConcurrentReadSnapshotsDuringChanges()
{
    WorkingFiles files;
    AbsolutePath const path = MakePath("concurrent.cpp");
    auto file = OpenFile(files, path, "value 0\n");
    std::atomic<bool> stop {false};
    std::atomic<bool> saw_bad_snapshot {false};

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back(
            [&]()
            {
                while (!stop.load(std::memory_order_relaxed))
                {
                    std::string content;
                    if (files.GetFileBufferContent(file, content))
                    {
                        bool const complete =
                            content.find("value ") == 0 && !content.empty() && content.back() == '\n';
                        if (!complete)
                        {
                            saw_bad_snapshot.store(true, std::memory_order_relaxed);
                        }
                    }
                }
            });
    }

    for (int i = 1; i <= 200; ++i)
    {
        lsTextDocumentDidChangeParams change;
        change.textDocument.uri = lsDocumentUri(path);
        change.textDocument.version = optional<int>(i + 1);
        lsTextDocumentContentChangeEvent edit;
        edit.text = "value " + std::to_string(i) + "\n";
        change.contentChanges.push_back(edit);
        files.OnChange(change);
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto& reader : readers)
    {
        reader.join();
    }

    Expect(!saw_bad_snapshot.load(std::memory_order_relaxed), "concurrent readers must only observe complete snapshots");
    Expect(ContentOf(files, file) == "value 200\n", "last concurrent change must win");
}

} // namespace

int main()
{
    TestOffsetBoundariesAndLineEndings();
    TestUtf8OffsetsUseCodePointCounting();
    TestMultipleEditsRebuildLineOffsetsSequentially();
    TestFullReplaceAndDocumentBoundaryEdits();
    TestLifecycleOpenCloseClearAndDirectories();
    TestOnSaveWritesCurrentContent();
    TestConcurrentReadSnapshotsDuringChanges();
    return test::Failures() == 0 ? 0 : 1;
}
