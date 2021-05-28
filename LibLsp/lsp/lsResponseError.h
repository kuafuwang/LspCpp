#pragma once

#include "LibLsp/JsonRpc/serializer.h"
#include <sstream>
#include "LibLsp/lsp/lsAny.h"

enum class lsErrorCodes:int32_t {
	// Defined by JSON RPC
	ParseError = -32700,
	InvalidRequest = -32600,
	MethodNotFound = -32601,
	InvalidParams = -32602,
	InternalError = -32603,
	serverErrorStart = -32099,
	serverErrorEnd = -32000,
	ServerNotInitialized = -32002,
	UnknownErrorCode = -32001,

	// Defined by the protocol.
	RequestCancelled = -32800,
};
MAKE_REFLECT_TYPE_PROXY(lsErrorCodes);
struct lsResponseError {
	
	inline operator bool() const { return message.size() > 0; }
	lsErrorCodes code;
	// Short description.
	std::string message;
	void swap(lsResponseError& arg) noexcept
	{
		message.swap(arg.message);
		std::swap(code, arg.code);
	}
	boost::optional<lsp::Any> data;
	std::string ToString();
	void Write(Writer& visitor);
};
MAKE_REFLECT_STRUCT(lsResponseError, code, message, data);