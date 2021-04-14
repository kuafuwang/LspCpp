#pragma once
#include <memory>
#include "RequestInMessage.h"


class PendingRequestInfo
{
	using   RequestCallBack = std::function< bool(std::unique_ptr<LspMessage>) >;
public:
	PendingRequestInfo(const std::string& md,
		const RequestCallBack& callback);
	PendingRequestInfo(const std::string& md);
	PendingRequestInfo(){}
	std::string method;
	RequestCallBack futureInfo;
};
