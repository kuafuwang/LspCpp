#pragma once

#include "LibLsp/JsonRpc/RemoteEndPoint.h"
#include "LibLsp/JsonRpc/RequestError.h"
#include "LibLsp/JsonRpc/traits.h"

namespace lsp
{

template<typename F, typename RequestType = ParamType<F, 0>, typename ResponseType = typename RequestType::Response>
bool BindLspHandler(RemoteEndPoint& endpoint, F&& handler)
{
    return endpoint.registerHandler(
        [handler = std::forward<F>(handler)](RequestType const& request) mutable -> ResponseOrError<ResponseType>
        {
            try
            {
                return handler(request);
            }
            catch (RequestError const&)
            {
                throw;
            }
            catch (std::exception const& ex)
            {
                throw RequestError(lsErrorCodes::InternalError, ex.what());
            }
        });
}

} // namespace lsp
