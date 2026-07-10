# 架构

本文描述 LspCpp 的组织方式以及消息在栈中的流转。

## 概览

LspCpp 有两个主要层：

```
┌─────────────────────────────────────────────────────────┐
│  LSP 层 (include/LibLsp/lsp/)                           │
│  类型化请求/通知、能力、辅助工具                          │
└──────────────────────────┬──────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────┐
│  JSON-RPC 层 (include/LibLsp/JsonRpc/)                  │
│  帧格式、解析、分发、endpoint、传输                        │
└──────────────────────────┬──────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────┐
│  第三方 / 捆绑依赖                                        │
│  Asio、RapidJSON、utfcpp、IXWebSocket                   │
└─────────────────────────────────────────────────────────┘
```

## 目录布局

```
LspCpp/
├── include/LibLsp/
│   ├── LspCpp.h              # 便利 umbrella 头文件
│   ├── JsonRpc/              # JSON-RPC 基础设施
│   └── lsp/                  # LSP 协议类型与辅助工具
├── src/
│   ├── jsonrpc/              # JSON-RPC 实现 (.cpp)
│   └── lsp/                  # LSP 辅助工具 (.cpp)
├── examples/                 # 示例服务器与客户端
├── tests/                    # CTest 冒烟测试
├── third_party/              # 捆绑依赖
├── ports/lspcpp/             # vcpkg overlay port
└── docs/                     # 文档
```

## JSON-RPC 层

### 核心组件

| 组件 | 作用 |
|------|------|
| **`StreamMessageProducer`** | 从 `istream` 读取 content-length 帧消息体 |
| **`MessageJsonHandler`** | 将 method 名映射到 JSON 解析/序列化函数 |
| **`ProtocolJsonHandler`** | LSP 专用 `MessageJsonHandler`，预注册所有标准 method |
| **`RemoteEndPoint`** | 入站分发、出站请求、handler 注册、工作线程 |
| **`GenericEndpoint` / `Endpoint`** | 在线路上发送消息 |
| **`stream.h`** | 抽象 `istream`/`ostream`、stdio 适配器、LSP 帧格式 |

### 消息流（服务器）

```
stdin / TCP / WebSocket
        │
        ▼
  StreamMessageProducer  ──►  parse 池：parse JSON
                                      │
                                      ▼
                              sequence 重排序缓冲
        │                                              │
        │                                              ▼
        │                                    typed request/notification
        │                                              │
        ▼                                              ▼
 Ordered dispatcher                      RemoteEndPoint::registerHandler
        │                                              │
        ├── notifications: FIFO 串行线程 ───────────────┤
        ├── opt-out notifications: handler 线程池 ─────┤
        ├── requests: 等待前序通知完成 ─────────────────┘
        └── responses: 精确 pending 完成
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

`RemoteEndPoint` 将消息读取、解析与 handler 执行分离。入站消息体按线路顺序分配 sequence，在专用 parse 池中解析，然后由重排序缓冲按 sequence 连续放行，因此路由仍然遵守线路顺序：

- 普通通知在专用 FIFO 通知线程上串行执行，因此 `textDocument/didOpen`、增量 `textDocument/didChange` 等顺序敏感通知会按线路顺序生效；
- 通过 `allowConcurrentNotification` 显式标记的方法会绕过 FIFO 通知线程并直接在 handler 池执行；这些通知不保序，也不会门控后续请求，因此只应给顺序不敏感的 handler 开启；
- 请求只有在其之前的所有通知执行完成后才会投递到 handler 池，因此请求 handler 能看到前序通知建立的文档状态；
- 请求之间仍可并发执行；构造函数参数 `max_workers` 控制 handler 池并发度；
- 出站请求的响应会匹配解析时捕获的精确 pending request。future 与阻塞等待 API 会在路由阶段直接完成 promise/condition，且不执行用户代码，因此同步 handler 在等待响应时不需要额外空闲的 handler worker。callback 形式的 send API 会先原子移除 pending，再把用户 callback 延后投递到 handler 池，以保留原有 callback 执行语义；
- `$/cancelRequest` 绕过通知队列。取消只会被前序帧的解析与 sequence 重排序延迟，不会被慢通知 handler 延迟。解析使用独立线程池，因此阻塞的同步 handler 不会饿死取消消息的解析；
- 每次 `startProcessingMessages` 运行都拥有独立的 active session output state。handler 捕获该 state，因此在 `stop()` 之后才返回的旧 handler 响应会被丢弃，不会写入后续 restart 的新会话；
- 嵌入方可以通过 `RemoteEndPointLimits` 配置 frame size、parse backlog、reorder buffer、FIFO notification queue、parked request queue、pending cancel 表和 seen request-id 表的上限。默认所有上限关闭；启用上限后，默认过载策略会停止当前 session，而不是静默丢弃有序 JSON-RPC 流中的消息并破坏协议状态；
- 显式 handler 注册、parser override、自定义 response parser 注册和 `allowConcurrentNotification` 都是 start 前契约。运行期解析和 handler dispatch 不会给注册表加锁；`ProtocolJsonHandler` 会在构造时预注册标准 LSP parser，自定义扩展也必须在 `startProcessingMessages()` 前完成注册。注册 API 会通过返回 `false`、记录 warning，并在 Debug 构建下触发断言来报告误用；
- 如果将来确实出现运行期注册需求，不应回到在 parse/handler 热路径加互斥锁。优先采用 snapshot-swap 设计：注册表通过 `std::shared_ptr<const map>` 快照持有，写侧 copy-on-write 后原子替换指针，读侧只付出一次原子加载快照的成本。

### 传输

| 文件 | 传输 |
|------|------|
| `stream.h` / `StreamMessageProducer.cpp` | stdio 与通用流 |
| `TcpServer.cpp` / `TcpServer.h` | TCP accept 循环，每连接一个 endpoint |
| `WebSocketServer.cpp` | WebSocket（IXWebSocket） |

## LSP 层

### 协议类型

每个 LSP method 都有通过 `LibLsp/JsonRpc/RequestInMessage.h` 和 `LibLsp/JsonRpc/NotificationInMessage.h` 中宏生成的 C++ 结构体：

- **`DEFINE_REQUEST_RESPONSE_TYPE`** — 请求 + 响应对（如 `td_initialize`）
- **`DEFINE_NOTIFICATION_TYPE`** — 客户端/服务器通知

类型使用 RapidJSON reflection（`MAKE_REFLECT_STRUCT`、`ReflectReader`）做序列化。

头文件镜像 LSP 规范结构：

- `general/` — 生命周期（`initialize`、`shutdown`、`exit`）
- `textDocument/` — 文档功能
- `workspace/` — 工作区功能
- `client/` — 客户端侧注册
- `extention/jdtls/` — Eclipse JDT.LS 扩展（遗留）

### 辅助模块

| 模块 | 用途 |
|------|------|
| **`WorkingFiles`** | 跟踪打开缓冲区、增量编辑、行偏移 |
| **`utils.cpp`** | URI 处理、路径规范化、UTF-8/UTF-16 转换 |
| **`lsp_diagnostic`** | 诊断构造辅助 |
| **`Markup.cpp`** | hover/文档的 MarkupContent |
| **`ParentProcessWatcher`** | 检测父编辑器进程退出 |

## LanguageSession

作为薄便利封装添加（`include/LibLsp/lsp/LanguageSession.h`）：

```cpp
LanguageSession
  ├── ProtocolJsonHandler   (shared, all LSP methods known)
  ├── GenericEndpoint       (outbound send)
  └── RemoteEndPoint        (inbound dispatch)
```

它不增加新协议行为；只是减少常见 stdio 服务器场景的样板代码。

## 线程与取消

- 消息读取在 `RemoteEndPoint` 内专用线程上运行。
- Handler 在内部队列的工作线程上运行。
- `CancelMonitor` 是传给 handler 的可调用对象，用于长时间请求；对应 LSP `$/cancelRequest`。
- `Condition<T>` 提供简单的 wait/notify 原语，示例中用于关闭信号。

## 可选：Boehm GC

当 `LSPCPP_SUPPORT_BOEHM_GC=ON` 时，`GCThreadContext` 将 Boehm GC 与工作线程模型集成。大多数部署保持关闭。

## 设计渊源

部分代码源自 [cquery](https://github.com/cquery-project/cquery)。JSON reflection 与 handler 注册模式遵循该项目约定。

## 延伸阅读

- [协议类型](protocol-types.md) — 扩展 LSP 消息定义
- [构建与测试](build-and-test.md) — 运行验证这些层的测试
