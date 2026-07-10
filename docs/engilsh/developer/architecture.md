# Architecture

This document describes how LspCpp is organized and how messages flow through the stack.

## Overview

LspCpp has two major layers:

```
┌─────────────────────────────────────────────────────────┐
│  LSP layer (include/LibLsp/lsp/)                        │
│  Typed requests/notifications, capabilities, helpers    │
└──────────────────────────┬──────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────┐
│  JSON-RPC layer (include/LibLsp/JsonRpc/)               │
│  Framing, parsing, dispatch, endpoints, transports      │
└──────────────────────────┬──────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────┐
│  Third-party / bundled deps                             │
│  Asio, RapidJSON, utfcpp, IXWebSocket                   │
└─────────────────────────────────────────────────────────┘
```

## Directory layout

```
LspCpp/
├── include/LibLsp/
│   ├── LspCpp.h              # Convenience umbrella header
│   ├── JsonRpc/              # JSON-RPC infrastructure
│   └── lsp/                  # LSP protocol types and helpers
├── src/
│   ├── jsonrpc/              # JSON-RPC implementation (.cpp)
│   └── lsp/                  # LSP helpers (.cpp)
├── examples/                 # Sample servers and clients
├── tests/                    # CTest smoke tests
├── third_party/              # Bundled dependencies
├── ports/lspcpp/             # vcpkg overlay port
└── docs/                     # Documentation
```

## JSON-RPC layer

### Core components

| Component | Role |
|-----------|------|
| **`StreamMessageProducer`** | Reads content-length–framed message bodies from an `istream` |
| **`MessageJsonHandler`** | Maps method names to JSON parse/serialize functions |
| **`ProtocolJsonHandler`** | LSP-specific `MessageJsonHandler` with all standard methods registered |
| **`RemoteEndPoint`** | Incoming dispatch, outgoing requests, handler registration, worker threads |
| **`GenericEndpoint` / `Endpoint`** | Sends messages on the wire |
| **`stream.h`** | Abstract `istream`/`ostream`, stdio adapters, LSP framing |

### Message flow (server)

```
stdin / TCP / WebSocket
        │
        ▼
  StreamMessageProducer  ──►  parse pool: parse JSON
                                      │
                                      ▼
                             sequence reorder buffer
        │                                              │
        │                                              ▼
        │                                    typed request/notification
        │                                              │
        ▼                                              ▼
 Ordered dispatcher                      RemoteEndPoint::registerHandler
        │                                              │
        ├── notifications: FIFO serial thread ─────────┤
        ├── opt-out notifications: handler pool ───────┤
        ├── requests: wait for prior notifications ────┘
        └── responses: handler pool
                                                       │
                                                       ▼
                                              user handler (lambda)
                                                       │
                                                       ▼
                                              serialize response
                                                       │
                                                       ▼
                                                   stdout / socket
```

`RemoteEndPoint` keeps message reading separate from parsing and handler execution. Incoming message bodies are assigned a sequence number in wire order, parsed in a dedicated parse pool, then released by a reorder buffer so routing still observes wire order:

- ordinary notifications run on a dedicated FIFO notification thread, so order-sensitive notifications such as `textDocument/didOpen` and incremental `textDocument/didChange` are applied in wire order;
- methods explicitly marked with `allowConcurrentNotification` bypass the FIFO notification thread and run on the handler pool; these notifications are not ordered and do not gate following requests, so only order-insensitive handlers should opt in;
- requests are posted to the handler pool only after all earlier notifications have completed, so request handlers observe document state established by prior notifications;
- requests may still run concurrently with each other; the `max_workers` constructor parameter controls this request/response handler-pool concurrency;
- responses and `$/cancelRequest` bypass the notification queue. Responses are matched by id, while cancellation is delayed only by parsing and sequence reordering of earlier frames, not by slow notification handlers. Parsing uses a separate pool so blocked synchronous handlers cannot starve cancellation parsing.

### Transports

| File | Transport |
|------|-----------|
| `stream.h` / `StreamMessageProducer.cpp` | stdio and generic streams |
| `TcpServer.cpp` / `TcpServer.h` | TCP accept loop, one endpoint per connection |
| `WebSocketServer.cpp` | WebSocket (IXWebSocket) |

## LSP layer

### Protocol types

Each LSP method has C++ structs generated via macros in `LibLsp/JsonRpc/RequestInMessage.h` and `LibLsp/JsonRpc/NotificationInMessage.h`:

- **`DEFINE_REQUEST_RESPONSE_TYPE`** — request + response pair (e.g. `td_initialize`)
- **`DEFINE_NOTIFICATION_TYPE`** — client/server notifications

Types use RapidJSON reflection (`MAKE_REFLECT_STRUCT`, `ReflectReader`) for serialization.

Headers mirror the LSP spec structure:

- `general/` — lifecycle (`initialize`, `shutdown`, `exit`)
- `textDocument/` — document features
- `workspace/` — workspace features
- `client/` — client-side registration
- `extention/jdtls/` — Eclipse JDT.LS extensions (legacy)

### Helpers

| Module | Purpose |
|--------|---------|
| **`WorkingFiles`** | Track open buffers, incremental edits, line offsets |
| **`utils.cpp`** | URI handling, path normalization, UTF-8/UTF-16 conversion |
| **`lsp_diagnostic`** | Diagnostic construction helpers |
| **`Markup.cpp`** | MarkupContent for hover/documentation |
| **`ParentProcessWatcher`** | Detect when the parent editor process exits |

## LanguageSession

Added as a thin convenience wrapper (`include/LibLsp/lsp/LanguageSession.h`):

```cpp
LanguageSession
  ├── ProtocolJsonHandler   (shared, all LSP methods known)
  ├── GenericEndpoint       (outbound send)
  └── RemoteEndPoint        (inbound dispatch)
```

It does not add new protocol behavior; it reduces boilerplate for the common stdio-server case.

## Threading and cancellation

- Message reading runs on a dedicated thread inside `RemoteEndPoint`.
- Handlers run on worker threads from the internal queue.
- `CancelMonitor` is a callable passed to handlers for long requests; it reflects LSP `$/cancelRequest`.
- `Condition<T>` provides a simple wait/notify primitive used in examples for shutdown signaling.

## Optional: Boehm GC

When `LSPCPP_SUPPORT_BOEHM_GC=ON`, `GCThreadContext` integrates Boehm GC with the worker thread model. Most deployments leave this off.

## Design lineage

Parts of the codebase derive from [cquery](https://github.com/cquery-project/cquery). The JSON reflection and handler registration patterns follow that project's conventions.

## Related reading

- [Protocol types](protocol-types.md) — extending LSP message definitions
- [Build and test](build-and-test.md) — running tests that exercise these layers
