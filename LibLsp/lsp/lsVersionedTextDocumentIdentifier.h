#pragma once

#include "LibLsp/JsonRpc/serializer.h"

#include "LibLsp/JsonRpc/message.h"
#include "LibLsp/lsp/lsDocumentUri.h"
#include "LibLsp/lsp/lsTextDocumentIdentifier.h"

struct lsVersionedTextDocumentIdentifier
{
	lsDocumentUri uri;
	// The version number of this document.  number | null
	boost::optional<int> version;

	lsTextDocumentIdentifier AsTextDocumentIdentifier() const;
	void swap(lsVersionedTextDocumentIdentifier& arg) noexcept
	{
		uri.swap(arg.uri);
		version.swap(arg.version);
		
	}
};
MAKE_REFLECT_STRUCT(lsVersionedTextDocumentIdentifier, uri, version);