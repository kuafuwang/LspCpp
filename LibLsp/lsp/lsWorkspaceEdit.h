#pragma once

#include "LibLsp/JsonRpc/serializer.h"
#include <vector>
#include "lsTextDocumentEdit.h"
#include "LibLsp/lsp/ResourceOperation.h"
#include "lsAny.h"

struct lsWorkspaceEdit {
	// Holds changes to existing resources.
	// changes ? : { [uri:string]: TextEdit[]; };
	// std::unordered_map<lsDocumentUri, std::vector<lsTextEdit>> changes;

	// An array of `TextDocumentEdit`s to express changes to specific a specific
	// version of a text document. Whether a client supports versioned document
	// edits is expressed via `WorkspaceClientCapabilites.versionedWorkspaceEdit`.
	//
	boost::optional< std::map<std::string, std::vector<lsTextEdit> > >  changes;
	typedef std::pair < boost::optional<lsTextDocumentEdit>, boost::optional<lsp::Any> > Either;

	boost::optional <  std::vector< Either > > documentChanges;
	~lsWorkspaceEdit();
	
	MAKE_SWAP_METHOD(lsWorkspaceEdit, changes, documentChanges);
};
MAKE_REFLECT_STRUCT(lsWorkspaceEdit, changes, documentChanges);

extern void Reflect(Reader& visitor, lsWorkspaceEdit::Either& value);

