#pragma once

#include "LibLsp/JsonRpc/serializer.h"

#include <string>
#include <vector>
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include "LibLsp/JsonRpc/message.h"
#include <rapidjson/writer.h>
#include "LibLsp/JsonRpc/json.h"
namespace lsp
{
	struct Any
	{
		
		int jsonType = -1;

		
		template <typename  T>                       
		void  Get(T& value)
		{
			
			rapidjson::Document document;
			document.Parse(data.c_str(), data.length());
			if (document.HasParseError()) {
				// 提示
				return ;
			}
			if(jsonType == -1)
			{
				jsonType = document.GetType();
			}
			JsonReader visitor{ &document };
			Reflect(visitor, value);
		}
		
		template <typename  T>
		void  Set(T& value)
		{
			rapidjson::StringBuffer output;
			rapidjson::Writer<rapidjson::StringBuffer> writer(output);
			JsonWriter json_writer{ &writer };
			Reflect(json_writer,value);
			data =  output.GetString();
			if(!data.empty())
			{
				if(data[0] == '{')
				{
					jsonType = rapidjson::kObjectType;
				}
				else if(data[0] == '[')
				{
					if (data.size() >= 2 && data[1] == '{')
						jsonType = rapidjson::kStringType;
					else
						jsonType = rapidjson::kArrayType;
				}
				else if (data[0] == '"')
				{
					jsonType = rapidjson::kStringType;
				}
			}
		}
		
		int GetType() 
		{
			if (jsonType == -1)
			{
				rapidjson::Document document;
				document.Parse(data.c_str(), data.length());
				if (document.HasParseError()) {
					// 提示
					assert(false);
				}
				jsonType = document.GetType();
			}
			return  jsonType;
		}
		void  Set(std::unique_ptr<LspMessage> value)
		{
			if(value)
			{
				jsonType = rapidjson::Type::kObjectType;
				data = value->ToJson();
			}
			else
			{
				assert(false);
			}
			
		}
		
		void SetJsonString(std::string&& _data,rapidjson::Type _type)
		{
			jsonType = _type;
			data.swap(_data);
			
		}
		void SetJsonString(const std::string& _data ,rapidjson::Type _type)
		{
			jsonType = _type;
			data=(_data);
			
		}
		const std::string& Data()const
		{
			
			return  data;
		}
		void swap(Any& arg) noexcept
		{
			data.swap(arg.data);
			int temp = jsonType;
			jsonType = arg.jsonType;
			arg.jsonType = temp;
		}
	private:
		std::string  data;
	};
};


extern void Reflect(Reader& visitor, lsp::Any& value);
extern  void Reflect(Writer& visitor, lsp::Any& value);

