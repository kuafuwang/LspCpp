#pragma once

#include "LibLsp/JsonRpc/serializer.h"
#include <string>
#include "LibLsp/JsonRpc/message.h"
#include <rapidjson/writer.h>

namespace lsp
{
	struct Any
	{
		//! Type of JSON value
		enum Type {
			kNullType = 0,      //!< null
			kFalseType = 1,     //!< false
			kTrueType = 2,      //!< true
			kObjectType = 3,    //!< object
			kArrayType = 4,     //!< array 
			kStringType = 5,    //!< string
			kNumberType = 6     //!< number
		};
		int jsonType = -1;

		
		template <typename  T>                       
		bool  Get(T& value)
		{
			const auto visitor = GetReader();
			Reflect(*visitor, value);
			return true;
		}
		
		template <typename  T>
		void  Set(T& value)
		{
			 auto visitor = GetWriter();
			Reflect(*visitor,value);
			SetData(visitor);
		}

		int GuessType();
		int GetType();

		void Set(std::unique_ptr<LspMessage> value);

		void SetJsonString(std::string&& _data,Type _type)
		{
			jsonType = _type;
			data.swap(_data);
		}
		void SetJsonString(const std::string& _data ,Type _type)
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
			const int temp = jsonType;
			jsonType = arg.jsonType;
			arg.jsonType = temp;
		}

	private:
		std::unique_ptr<Reader> GetReader();
		std::unique_ptr<Writer> GetWriter() const;
		void SetData(std::unique_ptr<Writer>&);
		std::string  data;
	};
};


extern void Reflect(Reader& visitor, lsp::Any& value);
extern  void Reflect(Writer& visitor, lsp::Any& value);

