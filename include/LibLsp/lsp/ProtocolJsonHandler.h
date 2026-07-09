#pragma once

#include "LibLsp/JsonRpc/MessageJsonHandler.h"
namespace lsp
{

struct ProtocolJsonHandlerOptions
{
    // JDTLS methods are Eclipse Java extension methods, not standard LSP.
    // Keep them opt-in so the default protocol handler stays standard-focused.
    bool enableJdtlsExtensions = false;
};

class ProtocolJsonHandler : public MessageJsonHandler
{
public:
    ProtocolJsonHandler();
    explicit ProtocolJsonHandler(ProtocolJsonHandlerOptions const& options);
};

} // namespace lsp
