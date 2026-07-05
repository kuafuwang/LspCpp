# 协议类型

LspCpp 如何在 C++ 中表示 LSP 消息以及如何扩展它们。

## 类型生成宏

请求和通知辅助宏位于 `include/LibLsp/JsonRpc/RequestInMessage.h` 和 `include/LibLsp/JsonRpc/NotificationInMessage.h`。Reflection 辅助位于 `include/LibLsp/JsonRpc/serializer.h`。

### 请求/响应对

```cpp
DEFINE_REQUEST_RESPONSE_TYPE(td_initialize, lsInitializeParams, InitializeResult, "initialize");
```

展开为：

- `td_initialize::request` — 入站请求结构体
- `td_initialize::response` — 出站响应结构体
- `td_initialize::request::kMethodInfo` — method 名常量（`"initialize"`）
- 通过 `ReflectReader` / `ToJson()` 的 JSON 解析/序列化钩子

### 通知

```cpp
DEFINE_NOTIFICATION_TYPE(Notify_TextDocumentDidOpen, TextDocumentDidOpen::Params, "textDocument/didOpen");
```

展开为 `Notify_TextDocumentDidOpen::notify`，无响应类型。

### 结构体 reflection

数据字段使用 RapidJSON reflection 宏：

```cpp
struct InitializeResult {
    lsServerCapabilities capabilities;
    MAKE_SWAP_METHOD(InitializeResult, capabilities);
};
MAKE_REFLECT_STRUCT(InitializeResult, capabilities);
```

`MAKE_REFLECT_STRUCT` 生成用于反序列化的 `ReflectReader` 和用于序列化的 `ToJson()`。

## 请求类型结构

生成的 request 继承自 `lsRequest<Params, request>`，后者继承自 `RequestInMessage`。概念上暴露：

```cpp
struct request : lsRequest<ParamsType, request> {
    static constexpr MethodType kMethodInfo = "textDocument/definition";
    using Response = response;
    // params 继承自 lsRequest<ParamsType, request>
    // id 和 method 继承自 RequestInMessage
    // ReflectReader、ToJson 等
};
```

Response 继承自 `ResponseMessage<ResultType, response>`，暴露 `result` 及继承的 JSON-RPC 响应字段：

```cpp
struct response : ResponseMessage<InitializeResult, response> {
    // result 继承自 ResponseMessage<InitializeResult, response>
    // id 继承自 ResponseInMessage
};
```

## Handler 注册

`RemoteEndPoint::registerHandler` 从 handler 签名推断 method：

```cpp
// Request: 第一个参数是 td_initialize::request const&
remote_end_point.registerHandler([](td_initialize::request const& req) {
    td_initialize::response rsp;
    rsp.id = req.id;
    return rsp;
});

// Notification: 第一个参数是 Notify_Exit::notify const&
remote_end_point.registerHandler([](Notify_Exit::notify const&) { });
```

返回类型：

- 请求：`T`、`lsp::ResponseOrError<T>`、`lsp::future<T>` 或 `lsp::future<lsp::ResponseOrError<T>>`
- 通知：`void`

可取消请求的第二个参数是 `CancelMonitor const&`，同步与异步 handler 均适用。

抛出 `lsp::RequestError` 时，handler 包装层会将其转换为 `Rsp_Error` 并发送。

## ProtocolJsonHandler

`src/lsp/ProtocolJsonHandler.cpp` 为每个已知 LSP method 注册 JSON 解析器。添加新 method 时：

1. 用 `DEFINE_*` 宏创建头文件。
2. 在 `src/lsp/ProtocolJsonHandler.cpp` 中包含该头文件。
3. 在相应辅助函数中注册解析器（`AddStandardRequestJsonRpcMethod`、`AddNotifyJsonRpcMethod` 或某响应辅助函数）。

运行时，`StreamMessageProducer` 在此 handler 中查找 method 字符串，在 dispatch 前反序列化 params。

## 命名约定

| 前缀 | 含义 | 示例 |
|------|------|------|
| `td_` | textDocument 请求 | `td_hover`、`td_completion` |
| `Notify_` | 通知 | `Notify_TextDocumentDidOpen` |
| 结构体 `ls` 前缀 | LSP 数据类型 | `lsPosition`、`lsRange`、`lsDiagnostic` |

Method 结构体名缩写 LSP 路径：

- `textDocument/definition` → `td_definition`
- `textDocument/didOpen` → `Notify_TextDocumentDidOpen`

发明新名前，先在 `include/LibLsp/lsp/` 下搜索现有头文件。

## 协议版本

LspCpp 跟踪多个 LSP 版本：

| 测试文件 | 覆盖 |
|----------|------|
| `lsp_types_roundtrip_tests.cpp` | 核心类型、通用往返 |
| `lsp_3_16_17_tests.cpp` | LSP 3.16 和 3.17 新增 |
| `lsp_3_18_tests.cpp` | LSP 3.18 新增 |

版本专用头文件包括 `protocol_3_18.h` 和功能门控结构体。为新规范版本添加类型时，放在适当命名的头文件中，并扩展对应测试文件。

## 可选与 variant 字段

LSP 可选字段使用代码库中的辅助类型：

- `optional<T>` 包装（见 `optionalVersion.h`；C++17 构建中别名 `std::optional`）
- `lsp::Any` 用于弱类型 JSON 值

同一头文件中现有结构体决定 nullable 与 omitted 字段语义。

## 自定义 JSON 解析

默认 `ReflectReader` 不足时：

```cpp
session.overrideRequestParser(
    "myExtension/doThing",
    [](Reader& visitor) -> std::unique_ptr<LspMessage> {
        // custom parse logic
    });
```

或覆盖类型化解析器：

```cpp
session.overrideRequestParser<td_initialize::request>();
```

谨慎使用 — 当 JSON 形态与规范一致时，优先修正结构体 reflection。

## 错误响应

用 `Rsp_Error` 返回错误：

```cpp
Rsp_Error err;
err.id = req.id;
err.error.code = lsErrorCodes::InternalError;
err.error.message = "something went wrong";
return err;
```

或使用 `lsp::ResponseOrError<T>`：

```cpp
return lsp::ResponseOrError<td_foo::response>(err);
```

也可以抛出 `lsp::RequestError`（`include/LibLsp/JsonRpc/RequestError.h`）：

```cpp
throw lsp::RequestError(lsErrorCodes::InvalidParams, "invalid parameters");
```

标准错误码在 `lsErrorCodes`（见 LSP 规范附录）。

## JDT.LS 扩展

`include/LibLsp/lsp/extention/jdtls/` 目录包含 Eclipse JDT Language Server 扩展，为基于 LspCpp 的 Java 工具保留兼容性。这些不属于标准 LSP 规范。

## 测试新类型

添加将结构体序列化为 JSON 再解析回来的往返测试：

```cpp
// lsp_types_roundtrip_tests.cpp 中的模式
auto json = value.ToJson();
Reader reader(json);
auto parsed = SomeType::ReflectReader(reader);
Expect(parsed.field == value.field, "field must round-trip");
```

可尽早发现 reflection 宏错误。

## 延伸阅读

- [LSP 规范](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)
- [架构](architecture.md) — 解析与分发在栈中的位置
- [编写语言服务器](../user/writing-a-language-server.md) — 在应用代码中使用类型
