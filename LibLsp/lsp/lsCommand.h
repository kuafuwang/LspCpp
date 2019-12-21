#pragma once

#include "LibLsp/JsonRpc/serializer.h"



#include <string>
#include <vector>
#include "lsAny.h"


template <typename AnyArray>
struct lsCommand {
	// Title of the command (ie, 'save')
	std::string title;
	// Actual command identifier.
	std::string command;
	// Arguments to run the command with.
	// **NOTE** This must be serialized as an array. Use
	// MAKE_REFLECT_STRUCT_WRITER_AS_ARRAY.
	optional<AnyArray> arguments;

	void swap(lsCommand<AnyArray>& arg) noexcept
	{
		title.swap(arg.title);
		command.swap(arg.command);
		arguments.swap(arg.arguments);
	}
};
template <typename TVisitor, typename T>
void Reflect(TVisitor& visitor, lsCommand<T>& value) {
	REFLECT_MEMBER_START();
	REFLECT_MEMBER(title);
	REFLECT_MEMBER(command);
	REFLECT_MEMBER(arguments);
	REFLECT_MEMBER_END();
}


using  lsCommandWithAny = lsCommand< std::vector<lsp::Any>>;
