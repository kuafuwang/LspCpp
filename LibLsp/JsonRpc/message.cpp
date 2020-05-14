#include "message.h"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "serializer.h"
#include "lsRequestId.h"
#include "RequestInMessage.h"
#include "Condition.h"
#include "PendingRequestInfo.h"
#include "json.h"


void LspMessage::Write(std::ostream& out) {
	rapidjson::StringBuffer output;
	rapidjson::Writer<rapidjson::StringBuffer> writer(output);
	JsonWriter json_writer{ &writer };
	ReflectWriter(json_writer);

	out << "Content-Length: " << output.GetSize() << "\r\n\r\n"
		<< output.GetString();
	out.flush();
}


std::string LspMessage::ToJson() 
{
	//return GetJson(const_cast<std::unique_ptr<LspMessage>> (this));
	rapidjson::StringBuffer output;
	rapidjson::Writer<rapidjson::StringBuffer> writer(output);
	JsonWriter json_writer{ &writer };
	this->ReflectWriter(json_writer);
	return  output.GetString();
}



PendingRequestInfo::PendingRequestInfo(const std::string& _md,
	const	RequestCallFun& callback) : method(_md),
	futureInfo(callback)
{
}

PendingRequestInfo::PendingRequestInfo(const std::string& md): method(md)
{
}


void Reflect(Reader& visitor, lsRequestId& value) {
	if (visitor.IsInt()) {
		value.type = lsRequestId::kInt;
		value.value = visitor.GetInt();
	}
	else if (visitor.IsInt64()) {
		value.type = lsRequestId::kInt;
		// `lsRequestId.value` is an `int`, so we're forced to truncate.
		value.value = static_cast<int>(visitor.GetInt64());
	}
	else if (visitor.IsString()) {
		value.type = lsRequestId::kString;
		value.k_string = visitor.GetString();
		value.value = atoi(value.k_string.c_str());
		
	}
	else {
		value.type = lsRequestId::kNone;
		value.value = -1;
	}
}

void Reflect(Writer& visitor, lsRequestId& value) {
	switch (value.type) {
	case lsRequestId::kNone:
		visitor.Null();
		break;
	case lsRequestId::kInt:
		visitor.Int(value.value);
		break;
	case lsRequestId::kString:
		
		if(value.k_string.empty())
		{
			std::string str = std::to_string(value.value);
			visitor.String(str.c_str(), str.length());
		}
		else
		{
			visitor.String(value.k_string.c_str(), value.k_string.length());
		}
		break;
	}
}

std::string ToString(const lsRequestId& id) {
	if (id.type != lsRequestId::kNone)
	{
		if(id.type == lsRequestId::kString)
		{
			if (!id.k_string.empty())
				return id.k_string;
		}
		return std::to_string(id.value);
	}
	
	return "";
}


