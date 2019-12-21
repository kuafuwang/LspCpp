#pragma once

#include "LibLsp/JsonRpc/serializer.h"

struct lsFormattingOptions {
	// Size of a tab in spaces.
	int tabSize =4;
	// Prefer spaces over tabs.
	bool insertSpaces = true;

	void swap(lsFormattingOptions& arg) noexcept
	{
		std::swap(tabSize, arg.tabSize);
		std::swap(insertSpaces, arg.insertSpaces);
	}
};
MAKE_REFLECT_STRUCT(lsFormattingOptions, tabSize, insertSpaces);