
#include "StreamMessageProducer.h"
#include <cassert>


bool StartsWith(std::string value, std::string start);
bool StartsWith(std::string value, std::string start) {
	if (start.size() > value.size())
		return false;
	return std::equal(start.begin(), start.end(), value.begin());
}

using  namespace std;
namespace
{
	const char* kContentLengthStart = "Content-Length: ";
}

//void StreamMessageProducer::listen(MessageConsumer callBack)
//{
//	keepRunning = true;
//	bool newLine = false;
//	Headers headers;
//	 // Read the content length. It is terminated by the "\r\n" sequence.
//	 while (keepRunning)
//	 {
//		 int exit_seq = 0;
//		 std::string stringified_content_length;
//		 stringified_content_length.reserve(15);
//		 while (true) {
//		 	if(input.bad())
//		 	{
//				MessageIssue issue(L"input stream corrupted", lsp::Log::Level::INFO);
//				issueHandler.handle(std::move(issue));
//				return;
//		 	}
//		 	if(input.fail())
//		 	{
//				MessageIssue issue(L"input fail", lsp::Log::Level::INFO);
//				issueHandler.handle(std::move(issue));
//				continue;
//		 	}
//		 
//			 auto c = input.get();
//			 if (EOF == c) {
//				 MessageIssue issue(L"No more input when reading content length header", lsp::Log::Level::INFO);
//				 issueHandler.handle(std::move(issue));
//				 return;
//			 }
//			
//
//			 if (exit_seq == 0 && c == '\r')
//				 ++exit_seq;
//			 if (exit_seq == 1 && c == '\n')
//				 break;
//
//			 stringified_content_length += c;
//		 }
//		 if(!StartsWith(stringified_content_length, kContentLengthStart))
//		 {
//			 MessageIssue issue(L"Unexpected token (expected Content-Length: sequence)", lsp::Log::Level::WARNING);
//			 issueHandler.handle(std::move(issue));
//		 	continue;
//		 }
//		 int content_length =
//			 atoi(stringified_content_length.c_str() + strlen(kContentLengthStart));
//
//		 // There is always a "\r\n" sequence before the actual content.
//		 auto expect_char = [&](char expected) {
//			auto opt_c = input.get();
//			 return opt_c == expected;
//		 };
//	 	
//		auto opt_c = input.get();
//	 	if(opt_c != '\r')
//	 	{
//			MessageIssue issue(L"Unexpected token (expected \\r\\n sequence)",lsp::Log::Level::WARNING);
//			issueHandler.handle(std::move(issue));
//			continue;
//	 	}
//		opt_c = input.get();
//		if (opt_c != '\n')
//		{
//			MessageIssue issue(L"Unexpected token (expected \\r\\n sequence)", lsp::Log::Level::WARNING);
//			issueHandler.handle(std::move(issue));
//			continue;
//		}
//		
//		 // Read content.
//		 std::string content(content_length,0);
//		 auto data = &content[0];
//		 for (int i = 0; i < content_length; ++i) {
//			 auto c = input.get();
//			 if (EOF==c) {
//				 MessageIssue issue(L"No more input when reading content body", lsp::Log::Level::INFO);
//				 issueHandler.handle(std::move(issue));
//				 return;
//			 }
//			 data[i] = c;
//		 }
//
//		 if(!callBack(std::move(content)))
//		 {
//			 return;
//		 }
//	 }
//	
//}

  string JSONRPC_VERSION = "2.0";
  string CONTENT_LENGTH_HEADER = "Content-Length";
  string CONTENT_TYPE_HEADER = "Content-Type";
  string JSON_MIME_TYPE = "application/json";
  string CRLF = "\r\n";

  void StreamMessageProducer::parseHeader(std::string& line, StreamMessageProducer::Headers& headers)
  {
	  int sepIndex = line.find(':');
	  if (sepIndex >= 0) {
		  auto key = line.substr(0, sepIndex);
	      if(key == CONTENT_LENGTH_HEADER)
	      {	
			headers.contentLength = atoi(line.substr(sepIndex + 1).data());
	      }
		  else if(key == CONTENT_TYPE_HEADER)
		  {
			  int charsetIndex = line.find("charset=");
			  if (charsetIndex >= 0)
				  headers.charset = line.substr(charsetIndex + 8);
		  }
	  }
  }
  

void StreamMessageProducer::listen(MessageConsumer callBack)
{
	keepRunning = true;
	bool newLine = false;
	Headers headers;
	string headerBuilder ;
	string debugBuilder ;
	// Read the content length. It is terminated by the "\r\n" sequence.
	while (keepRunning) 
	{
		if(input.bad())
		{
			MessageIssue issue(L"input stream corrupted", lsp::Log::Level::INFO);
			issueHandler.handle(std::move(issue));
			return;
		}
		if(input.fail())
		{
			MessageIssue issue(L"input fail", lsp::Log::Level::INFO);
			issueHandler.handle(std::move(issue));
			continue;
		}
		int c = input.get();
		if (c == EOF) {
			// End of input stream has been reached
			keepRunning = false;
		}
		else 
		{

			debugBuilder.push_back((char)c);
			if (c == '\n') 
			{
				if (newLine) {
					// Two consecutive newlines have been read, which signals the start of the message content
					if (headers.contentLength < 0) 
					{

						 MessageIssue issue(L"Unexpected token (expected Content-Length: sequence)", lsp::Log::Level::WARNING);
						 issueHandler.handle(std::move(issue));
					}
					else {
						bool result = handleMessage(headers,callBack);
						if (!result)
							keepRunning = false;
						
						newLine = false;
					}
					headers.clear();
					debugBuilder.clear();
				}
				else if (!headerBuilder.empty()) {
					// A single newline ends a header line
					parseHeader(headerBuilder, headers);
					headerBuilder.clear();
				}
				newLine = true;
			}
			else if (c != '\r') {
				// Add the input to the current header line
				
				headerBuilder.push_back((char)c);
				newLine = false;
			}
		}
	}

}
 bool StreamMessageProducer::handleMessage(Headers& headers ,MessageConsumer callBack)
{
	 		 // Read content.
	auto content_length = headers.contentLength;
 	 std::string content(content_length,0);
 	 auto data = &content[0];
 	 for (int i = 0; i < content_length; ++i) {
 		 auto c = input.get();
 		 if (EOF==c) {
 			 MessageIssue issue(L"No more input when reading content body", lsp::Log::Level::INFO);
 			 issueHandler.handle(std::move(issue));
 			 return false;
 		 }
 		 data[i] = c;
 	 }

 	 if(!callBack(std::move(content)))
 	 {
 		 return false;
 	 }
	return true;
}

