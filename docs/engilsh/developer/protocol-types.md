# Protocol types

How LspCpp represents LSP messages in C++ and how to extend them.

## Type generation macros

Request and notification helper macros live in `include/LibLsp/JsonRpc/RequestInMessage.h` and `include/LibLsp/JsonRpc/NotificationInMessage.h`. Reflection helpers live in `include/LibLsp/JsonRpc/serializer.h`.

### Request/response pairs

```cpp
DEFINE_REQUEST_RESPONSE_TYPE(td_initialize, lsInitializeParams, InitializeResult, "initialize");
```

Expands to:

- `td_initialize::request` — incoming request struct
- `td_initialize::response` — outgoing response struct
- `td_initialize::request::kMethodInfo` — method name constant (`"initialize"`)
- JSON parse/serialize hooks via `ReflectReader` / `ToJson()`

### Notifications

```cpp
DEFINE_NOTIFICATION_TYPE(Notify_TextDocumentDidOpen, TextDocumentDidOpen::Params, "textDocument/didOpen");
```

Expands to `Notify_TextDocumentDidOpen::notify` with no response type.

### Struct reflection

Data fields use RapidJSON reflection macros:

```cpp
struct InitializeResult {
    lsServerCapabilities capabilities;
    MAKE_SWAP_METHOD(InitializeResult, capabilities);
};
MAKE_REFLECT_STRUCT(InitializeResult, capabilities);
```

`MAKE_REFLECT_STRUCT` generates `ReflectReader` for deserialization and `ToJson()` for serialization.

## Request type anatomy

A generated request inherits from `lsRequest<Params, request>`, which inherits from `RequestInMessage`. Conceptually, it exposes:

```cpp
struct request : lsRequest<ParamsType, request> {
    static constexpr MethodType kMethodInfo = "textDocument/definition";
    using Response = response;
    // params is inherited from lsRequest<ParamsType, request>
    // id and method are inherited from RequestInMessage
    // ReflectReader, ToJson, etc.
};
```

Responses inherit from `ResponseMessage<ResultType, response>` and expose a `result` plus the inherited JSON-RPC response fields:

```cpp
struct response : ResponseMessage<InitializeResult, response> {
    // result is inherited from ResponseMessage<InitializeResult, response>
    // id is inherited from ResponseInMessage
};
```

## Handler registration

`RemoteEndPoint::registerHandler` deduces the method from the handler signature:

```cpp
// Request: first parameter is td_initialize::request const&
remote_end_point.registerHandler([](td_initialize::request const& req) {
    td_initialize::response rsp;
    rsp.id = req.id;
    return rsp;
});

// Notification: first parameter is Notify_Exit::notify const&
remote_end_point.registerHandler([](Notify_Exit::notify const&) { });
```

Return types:

- `T`, `lsp::ResponseOrError<T>`, `lsp::future<T>`, or `lsp::future<lsp::ResponseOrError<T>>` for requests
- `void` for notifications

For cancellable requests, the second parameter is `CancelMonitor const&` for both synchronous and async handlers.

Throwing `lsp::RequestError` is converted to an `Rsp_Error` response by the handler wrapper.

## ProtocolJsonHandler

`src/lsp/ProtocolJsonHandler.cpp` registers every known LSP method with its JSON parser. When you add a new method:

1. Create the header with `DEFINE_*` macros.
2. Include the header from `src/lsp/ProtocolJsonHandler.cpp`.
3. Register the parser in the relevant helper (`AddStandardRequestJsonRpcMethod`, `AddNotifyJsonRpcMethod`, or one of the response helpers).

At runtime, `StreamMessageProducer` looks up the method string in this handler to deserialize params before dispatch.

## Naming conventions

| Prefix | Meaning | Example |
|--------|---------|---------|
| `td_` | textDocument request | `td_hover`, `td_completion` |
| `Notify_` | Notification | `Notify_TextDocumentDidOpen` |
| `ls` prefix on structs | LSP data types | `lsPosition`, `lsRange`, `lsDiagnostic` |

Method struct names abbreviate the LSP path:

- `textDocument/definition` → `td_definition`
- `textDocument/didOpen` → `Notify_TextDocumentDidOpen`

Search existing headers under `include/LibLsp/lsp/` before inventing new names.

## Protocol versions

LspCpp tracks multiple LSP versions:

| Test file | Coverage |
|-----------|----------|
| `lsp_types_roundtrip_tests.cpp` | Core types, general round-trips |
| `lsp_3_16_17_tests.cpp` | LSP 3.16 and 3.17 additions |
| `lsp_3_18_tests.cpp` | LSP 3.18 additions |

Version-specific headers include `protocol_3_18.h` and feature-gated structs. When adding types for a new spec version, place them in a appropriately named header and extend the matching test file.

## Optional and variant fields

LSP optional fields use helper types from the codebase:

- `optional<T>` wrappers (see `optionalVersion.h`; this aliases `std::optional` in C++17 builds)
- `lsp::Any` for loosely typed JSON values

Follow existing structs in the same header for nullable vs. omitted field semantics.

## Custom JSON parsing

If the default `ReflectReader` is insufficient:

```cpp
session.overrideRequestParser(
    "myExtension/doThing",
    [](Reader& visitor) -> std::unique_ptr<LspMessage> {
        // custom parse logic
    });
```

Or override the typed parser:

```cpp
session.overrideRequestParser<td_initialize::request>();
```

Use sparingly—prefer fixing the struct reflection when the JSON shape matches the spec.

## Error responses

Return errors with `Rsp_Error`:

```cpp
Rsp_Error err;
err.id = req.id;
err.error.code = lsErrorCodes::InternalError;
err.error.message = "something went wrong";
return err;
```

Or use `lsp::ResponseOrError<T>`:

```cpp
return lsp::ResponseOrError<td_foo::response>(err);
```

You can also throw `lsp::RequestError` (`include/LibLsp/JsonRpc/RequestError.h`):

```cpp
throw lsp::RequestError(lsErrorCodes::InvalidParams, "invalid parameters");
```

Standard error codes are in `lsErrorCodes` (see LSP spec appendix).

## JDT.LS extensions

The `include/LibLsp/lsp/extention/jdtls/` directory contains Eclipse JDT Language Server extensions retained for compatibility with Java tooling built on LspCpp. These are not part of the standard LSP spec.

## Testing new types

Add round-trip tests that serialize a struct to JSON and parse it back:

```cpp
// Pattern used in lsp_types_roundtrip_tests.cpp
auto json = value.ToJson();
Reader reader(json);
auto parsed = SomeType::ReflectReader(reader);
Expect(parsed.field == value.field, "field must round-trip");
```

This catches reflection macro mistakes early.

## Further reading

- [LSP specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)
- [Architecture](architecture.md) — where parsing and dispatch fit in the stack
- [Writing a language server](../user/writing-a-language-server.md) — using types from application code
