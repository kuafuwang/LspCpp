#pragma once


#include "serializer.h"
#include <atomic>
#include <mutex>
#include "lsRequestId.h"
#include "LibLsp/JsonRpc/message.h"
#include "LibLsp/lsp/method_type.h"
#include "lsResponseMessage.h"
using RequestCallFun = std::function< bool(std::unique_ptr<LspMessage>) >;
struct RequestInMessage : public LspMessage {
	// number or string, actually no null
	lsRequestId id;
	std::string method;
	Kind GetKid() override
	{
		return  REQUEST_MESSAGE;
	}
};


template <class T, class TDerived >
struct lsRequest : public RequestInMessage
{
	lsRequest(MethodType _method)
	{
		method = _method;
	}
	void ReflectWriter(Writer& writer) override {
		Reflect(writer, static_cast<TDerived&>(*this));
	}
	
	static std::unique_ptr<LspMessage> ReflectReader(Reader& visitor) {

		TDerived* temp = new TDerived();
		std::unique_ptr<TDerived>  message = std::unique_ptr<TDerived>(temp);
		// Reflect may throw and *message will be partially deserialized.
		Reflect(visitor, static_cast<TDerived&>(*temp));
		return message;
	}
	void swap(lsRequest& arg) noexcept
	{
		id.swap(arg.id);
		method.swap(method);
		std::swap(params, arg.params);
	}
	T params;
};


#define DEFINE_REQUEST_RESPONSE_TYPE(MSG,request_param,response_result)\
namespace  MSG {\
	extern  MethodType  kMethodType;\
	struct request : public lsRequest< request_param , request >{\
		MethodType GetMethodType() const override { return kMethodType; }\
		request():lsRequest(kMethodType){}                                   \
		void SetMethodType(MethodType){}								  \
	};\
	struct response :public ResponseMessage< response_result , response>{};\
};\
MAKE_REFLECT_STRUCT(MSG::request, jsonrpc, id, method, params);\
MAKE_REFLECT_STRUCT(MSG::response, jsonrpc, id, result);



#define DEFINE_REQUEST_TYPE(MSG,request_param)\
namespace  MSG {\
	extern  MethodType  kMethodType;\
	struct request : public lsRequest< request_param , request >{\
		MethodType GetMethodType() const override { return kMethodType; }\
		request():lsRequest(kMethodType){}                                   \
		void SetMethodType(MethodType){}								  \
	};\
};\
MAKE_REFLECT_STRUCT(MSG::request, jsonrpc, id, method, params);


#define DEFINE_RESPONSE_TYPE(MSG,response_result)\
namespace  MSG {\
	struct response :public ResponseMessage< response_result , response>{};\
};\
MAKE_REFLECT_STRUCT(MSG::response, jsonrpc, id, result);
