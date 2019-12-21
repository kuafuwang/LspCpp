#pragma once

#include "LibLsp/JsonRpc/serializer.h"
#include <string>
#include <vector>
#include "lsDocumentUri.h"


struct lsTextDocumentItem {
	// The text document's URI.
	lsDocumentUri uri;

	// The text document's language identifier.
	std::string languageId;

	// The version number of this document (it will strictly increase after each
	// change, including undo/redo).
	int version = 0;

	// The content of the opened text document.
	std::string text;
	void swap(lsTextDocumentItem& arg) noexcept
	{
		uri.swap(arg.uri);
		text.swap(arg.text);
	}
};

MAKE_REFLECT_STRUCT(lsTextDocumentItem, uri, languageId, version, text);