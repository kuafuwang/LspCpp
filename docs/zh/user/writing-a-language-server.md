# 编写语言服务器

本指南介绍使用 LspCpp 实现 LSP 服务器的主要 API 模式。

## 两层 API

LspCpp 提供两种搭建服务器的方式：

| API | 头文件 | 适用场景 |
|-----|--------|----------|
| **`LanguageSession`** | `LibLsp/LspCpp.h` | 推荐用于新代码。`RemoteEndPoint` 的薄封装。 |
| **`RemoteEndPoint`** | `LibLsp/JsonRpc/RemoteEndPoint.h` | 更底层控制；旧示例中使用。 |

两者使用相同的 handler 注册模型。`LanguageSession` 是便于使用的入口：

```cpp
lsp::LanguageServer server;  // LanguageSession 的别名
```

## 注册 handler

通过 `on()`（LanguageSession）或 `registerHandler()`（RemoteEndPoint）注册 handler。编译器根据 handler 参数类型推断 LSP 方法。

### 请求 handler

```cpp
server.on([](td_initialize::request const& req) -> lsp::ResponseOrError<td_initialize::response> {
    td_initialize::response rsp;
    rsp.id = req.id;
    rsp.result.capabilities.hoverProvider = true;
    return rsp;
});
```

返回 `lsp::ResponseOrError<T>` 可发送成功响应或 JSON-RPC 错误。也可以直接返回响应类型；库在多数情况下会自动包装。

设置 `rsp.id = req.id`，以便客户端关联响应。

### 支持取消的请求 handler

长时间运行的请求可接受 `CancelMonitor`：

```cpp
server.on([](td_definition::request const& req, CancelMonitor const& monitor)
    -> lsp::ResponseOrError<td_definition::response>
{
    if (monitor && monitor()) {
        Rsp_Error err;
        err.error.code = lsErrorCodes::RequestCancelled;
        err.error.message = "cancelled";
        return err;
    }
    td_definition::response rsp;
    rsp.id = req.id;
    // ...
    return rsp;
});
```

### 通知 handler

通知没有响应。注册方式相同：

```cpp
server.on([](Notify_Exit::notify const&) {
    // handle exit
});

server.on([](Notify_TextDocumentDidOpen::notify const& n) {
    // textDocument/didOpen
});
```

## LSP 生命周期

规范的服务器遵循以下顺序：

```
Client                          Server
  |---- initialize ----------->  |
  |<--- InitializeResult ------|
  |---- initialized --------->  |  (notification, no response)
  |---- ... requests ... ---->  |
  |---- shutdown ------------>  |
  |<--- null ------------------|
  |---- exit ----------------->  |
  |                             | stop()
```

至少实现：

```cpp
server.on([](td_initialize::request const& req) { /* return capabilities */ });
server.on([](td_shutdown::request const& req) {
    td_shutdown::response rsp;
    rsp.id = req.id;
    return rsp;
});
server.on([&](Notify_Exit::notify const&) {
    exit_requested.notify(std::make_unique<bool>(true));
});
```

在 `initialize` 完成之前，服务器应拒绝其他请求（LspCpp 会强制协议解析；你的 handler 不应在初始化完成前假设文档已打开）。

## 声明能力

能力在 `initialize` 响应中返回：

```cpp
server.on([](td_initialize::request const& req) {
    td_initialize::response rsp;
    rsp.id = req.id;

    lsTextDocumentSyncOptions sync;
    sync.openClose = true;
    sync.change = lsTextDocumentSyncKind::Full;
    rsp.result.capabilities.textDocumentSync =
        std::make_pair(optional<lsTextDocumentSyncKind>(), optional<lsTextDocumentSyncOptions>(sync));

    lsCompletionOptions completion;
    completion.triggerCharacters = std::vector<std::string> { "." };
    rsp.result.capabilities.completionProvider = completion;

    rsp.result.capabilities.hoverProvider = true;

    WorkDoneProgressOptions definition_options;
    rsp.result.capabilities.definitionProvider =
        std::make_pair(optional<bool>(true), optional<WorkDoneProgressOptions>(definition_options));

    return rsp;
});
```

只声明你实际实现的功能。客户端据此决定发送哪些请求。

若干服务器 capability 字段对应 LSP 的 union 类型。在本代码库中，这些字段表示为 `std::pair<optional<...>, optional<...>>`；第一个槽位通常是简单的 boolean/kind 形式，第二个槽位是详细 options 对象。

## 发送出站消息

使用 `server.endpoint()` 向客户端发送通知或请求：

```cpp
// 记录到客户端输出面板
Notify_LogMessage::notify log;
log.params.type = lsMessageType::Log;
log.params.message = "indexing complete";
session.endpoint().send(log);

// 发布诊断
Notify_TextDocumentPublishDiagnostics::notify diag;
diag.params.uri.raw_uri_ = "file:///path/to/file.lang";
diag.params.diagnostics = { /* lsDiagnostic entries */ };
session.endpoint().send(diag);
```

## 跟踪打开的文档

LspCpp 提供 `WorkingFiles`，用于维护与 LSP 文档事件同步的内存缓冲区：

```cpp
#include "LibLsp/lsp/working_files.h"

WorkingFiles files;

server.on([&](Notify_TextDocumentDidOpen::notify const& n) {
    files.OnOpen(n.params.textDocument);
});

server.on([&](Notify_TextDocumentDidChange::notify const& n) {
    files.OnChange(n.params);
});

server.on([&](Notify_TextDocumentDidClose::notify const& n) {
    files.OnClose(n.params.textDocument);
});
```

`WorkingFile` 存储缓冲区内容和预计算的行偏移，用于将 LSP 位置转换为字节偏移。

## 日志

向 `LanguageSession` 传入自定义 `Log` 实现，便于开发时诊断：

```cpp
lsp::StderrLog log;
lsp::LanguageSession server(log);
```

生产环境若不需要 stderr 输出，使用 `NullLog`（默认）。

## 自定义请求解析

当需要对某方法做非标准参数处理时，可覆盖内置 JSON 解析器。解析器必须返回与该 method 的 handler 接受的相同具体请求类型：

```cpp
server.overrideRequestParser(
    td_initialize::request::kMethodInfo,
    [](Reader& visitor) {
        return td_initialize::request::ReflectReader(visitor);
    });
```

或使用模板快捷方式：

```cpp
server.overrideRequestParser<td_initialize::request>();
```

自定义请求/通知类型、厂商扩展以及替换内置消息形态，见[高级定制](advanced-customization.md)。

## 查找 LSP 类型头文件

请求和通知类型遵循命名约定：

| 模式 | 示例 | LSP 方法 |
|------|------|----------|
| `td_<feature>::request` | `td_definition::request` | `textDocument/definition` |
| `td_<feature>::response` | `td_definition::response` | （响应结构体） |
| `Notify_<Name>::notify` | `Notify_Exit::notify` | `exit` |

头文件按领域组织在 `include/LibLsp/lsp/` 下：

- `general/` — initialize、shutdown、exit
- `textDocument/` — completion、hover、rename 等
- `workspace/` — symbols、configuration、applyEdit

这些类型的生成与扩展方式见[协议类型](../developer/protocol-types.md)。

## 仓库中的示例

| 示例 | 说明 |
|------|------|
| `MinimalStdIOServerExample.cpp` | 最小 stdio 服务器（无 Boost） |
| `StdIOServerExample.cpp` | 带能力、definition handler、取消的 stdio 服务器 |
| `TcpServerExample.cpp` | 可配置端口的 TCP 服务器 |
| `WebsocketExample.cpp` | WebSocket 服务器 |
| `StdIOClientExample.cpp` | stdio 上的客户端 JSON-RPC |

使用 `-DLSPCPP_BUILD_EXAMPLES=ON` 构建全部示例（需要 Boost）。
