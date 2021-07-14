#pragma once

#include "LibLsp/JsonRpc/serializer.h"
#include <string>
#include "LibLsp/JsonRpc/message.h"
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


		/*
		 *Example for GetFromMap
			std::string data = "{\"visitor\":\"default\",\"verbose\":\"true\"}
			struct A{
				std::string  visitor;
				bool   verbose;
			}
			REFLECT_MAP_TO_STRUCT(A,visitor,verbose)

			lsp:Any any;
			any.SetJsonString(data, static_cast<lsp::Any::Type>(-1));
			A a_object;
			any.GetFromMap(a_object);
		*/
		template <typename  T>
		bool GetFromMap(T& value);

		template <typename  T>
		void  Set(T& value)
		{
			auto visitor = GetWriter();
			Reflect(*visitor, value);
			SetData(visitor);
		}

		int GuessType();
		int GetType();

		void Set(std::unique_ptr<LspMessage> value);

		void SetJsonString(std::string&& _data, Type _type)
		{
			jsonType = _type;
			data.swap(_data);
		}
		void SetJsonString(const std::string& _data, Type _type)
		{
			jsonType = _type;
			data = (_data);

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

template <typename T>
void ReflectMember(std::map < std::string, lsp::Any>& visitor, const char* name, T& value) {

	auto it = visitor.find(name);
	if (it != visitor.end())
	{
		it->second.Get(value);
	}
}
template <typename T>
void ReflectMember(std::map < std::string, std::string>& visitor, const char* name, T& value) {

	auto it = visitor.find(name);
	if (it != visitor.end())
	{
		lsp::Any any;
		any.SetJsonString(it->second, static_cast<lsp::Any::Type>(-1));
		any.Get(value);
	}
}

#define REFLECT_MAP_TO_STRUCT(type, ...)               \
  template <typename TVisitor>                       \
  void ReflectMap(TVisitor& visitor, type& value) {     \
    MACRO_MAP(_MAPPABLE_REFLECT_MEMBER, __VA_ARGS__) \
  }


namespace lsp
{
	template <typename T>
	bool Any::GetFromMap(T& value)
	{
		const auto visitor = GetReader();
		std::map < std::string, lsp::Any> _temp;
		Reflect(*visitor, _temp);
		ReflectMap(_temp, value);
		return true;
	}
}
