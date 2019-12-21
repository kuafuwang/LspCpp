#pragma once


#include "LibLsp/JsonRpc/RequestInMessage.h"
#include "LibLsp/JsonRpc/lsResponseMessage.h"
#include "LibLsp/lsp/symbol.h"
#include "LibLsp/lsp/lsTextDocumentPositionParams.h"
#include "LibLsp/lsp/lsRange.h"

enum  class CallHierarchyDirection : uint32_t
{
	CallsFrom=1,

	CallsTo=2
};
MAKE_REFLECT_TYPE_PROXY(CallHierarchyDirection);

struct CallHierarchyParams :public lsTextDocumentPositionParams
{
	CallHierarchyDirection direction = CallHierarchyDirection::CallsTo;
	MAKE_SWAP_METHOD(CallHierarchyParams, textDocument, position, direction)
};
MAKE_REFLECT_STRUCT(CallHierarchyParams, textDocument, position, direction);




 struct CallHierarchySymbol
{
	/**
	 * The name of the symbol targeted by the call hierarchy request.
	 */
	
		std::string name;

	/**
	 * More detail for this symbol, e.g the signature of a function.
	 */
 	optional<std::string>
	 detail;

	/**
	 * The kind of this symbol.
	 */
	
	SymbolKind kind;

	/**
	 * URI of the document containing the symbol.
	 */
	
	lsDocumentUri uri;

	/**
	 * The range enclosing this symbol not including leading/trailing whitespace but everything else
	 * like comments. This information is typically used to determine if the the clients cursor is
	 * inside the symbol to reveal in the symbol in the UI.
	 */
	
		lsRange range;

	/**
	 * The range that should be selected and revealed when this symbol is being picked, e.g the name of a function.
	 * Must be contained by the the {@link CallHierarchySymbol#getRange range}.
	 */
	
	 lsRange selectionRange;

	 MAKE_SWAP_METHOD(CallHierarchySymbol, name, detail, kind, uri, range, selectionRange)
};
 MAKE_REFLECT_STRUCT(CallHierarchySymbol, name, detail, kind, uri, range, selectionRange);

struct CallHierarchyCall
{
	/**
  * The source range of the reference. The range is a sub range of the {@link CallHierarchyCall#getFrom from} symbol range.
  */
	
		lsRange range;

	/**
	 * The symbol that contains the reference.
	 */
	
		 CallHierarchySymbol from;

	/**
	 * The symbol that is referenced.
	 */
	
		 CallHierarchySymbol to;

		 MAKE_SWAP_METHOD(CallHierarchyCall, range, from, to);
};
MAKE_REFLECT_STRUCT(CallHierarchyCall, range, from, to);



DEFINE_REQUEST_RESPONSE_TYPE(td_callHierarchy, CallHierarchyParams, std::vector<CallHierarchySymbol>);