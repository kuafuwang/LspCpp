#pragma once
#include <map>
#include <functional>
#include <memory>
#include "MessageIssue.h"
struct LspMessage;
struct NotificationInMessage;
struct lsBaseOutMessage;
struct  RequestInMessage;

class Endpoint
{
public:
	virtual  ~Endpoint() = default;
	virtual  bool onRequest(std::unique_ptr<LspMessage>) = 0;
	virtual  bool notify(std::unique_ptr<LspMessage>) = 0;
	
	virtual  bool onResponse(const std::string&, std::unique_ptr<LspMessage>) = 0;
	
};

class GenericEndpoint :public Endpoint
{
public:
	GenericEndpoint(lsp::Log& l):log(l){}
	bool notify(std::unique_ptr<LspMessage>) override;
	bool onResponse(const std::string&, std::unique_ptr<LspMessage>) override;

	bool onRequest(std::unique_ptr<LspMessage>) override;
	std::map< std::string, std::function< bool(std::unique_ptr<LspMessage>) > > method2request;
	std::map< std::string, std::function< bool(std::unique_ptr<LspMessage>) > > method2response;
	std::map< std::string, std::function< bool(std::unique_ptr<LspMessage>) >  > method2notification;
	lsp::Log& log;

};