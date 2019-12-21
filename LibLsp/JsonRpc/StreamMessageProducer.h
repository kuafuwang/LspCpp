#pragma once
#include <string>
#include <functional>
#include "MessageProducer.h"
#include <iostream>
#include "MessageIssue.h"

class StreamMessageProducer : public MessageProducer
{
public:
	struct Headers
	{
		int contentLength = -1;
		std::string charset;
		void clear()
		{
			contentLength = -1;
			charset.clear();
		}
	};
	bool handleMessage(Headers& headers, MessageConsumer callBack);
	StreamMessageProducer(
		MessageIssueHandler& message_issue_handler, std::istream& input)
		: issueHandler(message_issue_handler),
		  input(input)
	{
	}
	bool keepRunning = false;
	void listen(MessageConsumer) override;
	void parseHeader(std::string& line, StreamMessageProducer::Headers& headers);
private:
	MessageIssueHandler& issueHandler;
	 std::istream& input;
	

};
