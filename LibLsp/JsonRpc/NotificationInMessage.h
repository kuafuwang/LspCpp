#pragma once


#include "lsRequestId.h"
#include "LibLsp/JsonRpc/message.h"



// NotificationInMessage does not have |id|.
struct NotificationInMessage : public LspMessage {
	Kind GetKid() override
	{
		return  NOTIFICATION_MESSAGE;
	}
	
	std::string method;
};
template <class T, class TDerived >
struct lsNotificationInMessage : NotificationInMessage {
	
	void ReflectWriter(Writer& writer) override {
		Reflect(writer, static_cast<TDerived&>(*this));
	}
	lsNotificationInMessage(MethodType _method) 
	{
		method = _method;
	}
	static std::unique_ptr<LspMessage> ReflectReader(Reader& visitor) {

		TDerived* temp = new TDerived();
		
		std::unique_ptr<TDerived>  message = std::unique_ptr<TDerived>(temp);
		// Reflect may throw and *message will be partially deserialized.
		Reflect(visitor, static_cast<TDerived&>(*temp));
		return message;

	}
	void swap(lsNotificationInMessage& arg) noexcept
	{
		method.swap(method);
		std::swap(params, arg.params);
	}
	T params;
};

#define DEFINE_NOTIFICATION_TYPE(MSG,paramType)\
namespace  MSG {\
	extern  MethodType  kMethodType;\
	struct notify : public lsNotificationInMessage< paramType , notify >{\
		MethodType GetMethodType() const override { return kMethodType; }\
		notify():lsNotificationInMessage(kMethodType){}                                   \
		void SetMethodType(MethodType){}								  \
	};\
};\
MAKE_REFLECT_STRUCT(MSG::notify, jsonrpc,method, params)