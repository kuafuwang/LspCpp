#pragma once
#include "LibLsp/lsp/lsFormattingOptions.h"

#include "LibLsp/JsonRpc/RequestInMessage.h"
#include "LibLsp/JsonRpc/lsResponseMessage.h"


namespace  TextDocumentFormatting  {

  struct Params {
    lsTextDocumentIdentifier textDocument;
    lsFormattingOptions options;
	lsRange range;
	MAKE_SWAP_METHOD(Params, textDocument, options, range);
  };

};
MAKE_REFLECT_STRUCT(TextDocumentFormatting::Params, textDocument, options, range);
/**
 * The document formatting request is sent from the client to the server to
 * format a whole document.
 *
 * Registration Options: TextDocumentRegistrationOptions
 */
DEFINE_REQUEST_RESPONSE_TYPE(td_formatting, TextDocumentFormatting::Params,
	std::vector<lsTextEdit>);

