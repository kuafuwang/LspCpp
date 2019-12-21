#pragma once

#include "LibLsp/JsonRpc/serializer.h"



#include <string>
#include "lsRange.h"


struct lsTextEdit {
	// The range of the text document to be manipulated. To insert
	// text into a document create a range where start === end.
	lsRange range;

	// The string to be inserted. For delete operations use an
	// empty string.
	std::string newText;

	bool operator==(const lsTextEdit& that);
	std::string ToString() const;
	MAKE_SWAP_METHOD(lsTextEdit, range, newText);
};
MAKE_REFLECT_STRUCT(lsTextEdit, range, newText);
