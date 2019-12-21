#pragma once
#include <string>
#include <map>
#include <functional>
#include <LibLsp/JsonRpc/message.h>
class Reader;

class MessageJsonHandler
{
public:
	std::map< std::string, std::function< std::unique_ptr<LspMessage>(Reader&) > > method2response;
	std::map< std::string, std::function< std::unique_ptr<LspMessage>(Reader&) > > method2request;

	std::map< std::string, std::function<std::unique_ptr<LspMessage> (Reader&) > > method2notification;

	std::unique_ptr<LspMessage> parseResponseMessage(const std::string&, Reader&);
	std::unique_ptr<LspMessage> parseRequstMessage(const std::string&, Reader&);
	bool resovleResponseMessage(Reader&, std::pair<std::string, std::unique_ptr<LspMessage>>& result);
	std::unique_ptr<LspMessage> parseNotificationMessage(const std::string&, Reader&);
};

