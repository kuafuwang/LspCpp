#pragma once

#include "LibLsp/JsonRpc/serializer.h"
#include "lsDocumentUri.h"

struct lsTextDocumentIdentifier {
	lsDocumentUri uri;
	void swap(lsTextDocumentIdentifier& arg) noexcept
	{
		uri.swap(arg.uri);
	}
};
MAKE_REFLECT_STRUCT(lsTextDocumentIdentifier, uri);