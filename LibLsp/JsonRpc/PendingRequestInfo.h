#pragma once
#include <memory>
#include "RequestInMessage.h"


class PendingRequestInfo
{
	using   RequestCallFun = std::function< bool(std::unique_ptr<LspMessage>) >;
public:
	PendingRequestInfo(const std::string& md,
		const RequestCallFun& callback);
	PendingRequestInfo(const std::string& md);
	PendingRequestInfo(){}
	std::string method;
	RequestCallFun futureInfo;
};
