#pragma once
#include "LibLsp/lsp/method_type.h"


#include <stdexcept>
#include "LibLsp/JsonRpc/message.h"
#include "LibLsp/lsp/lsDocumentUri.h"
#include "LibLsp/lsp/lsAny.h"
#include "InitializeParams.h"




extern void Reflect(Reader&, std::pair<optional<lsTextDocumentSyncKind>, optional<lsTextDocumentSyncOptions> >&);

/**
 * Code Action options.
 */
struct  CodeActionOptions : WorkDoneProgressOptions {
	/**
	 * CodeActionKinds that this server may return.
	 *
	 * The list of kinds may be generic, such as `CodeActionKind.Refactor`, or the server
	 * may list out every specific kind they provide.
	 */
	typedef  std::string CodeActionKind;
	 std::vector<CodeActionKind> codeActionKinds;

	 MAKE_SWAP_METHOD(CodeActionOptions, workDoneProgress, codeActionKinds);
};
MAKE_REFLECT_STRUCT(CodeActionOptions, workDoneProgress, codeActionKinds)
struct CodeLensOptions : WorkDoneProgressOptions {
	/**
	 * Code lens has a resolve provider as well.
	 */
	optional<bool> resolveProvider ;
	MAKE_SWAP_METHOD(CodeLensOptions, workDoneProgress, resolveProvider);
};
MAKE_REFLECT_STRUCT(CodeLensOptions, workDoneProgress, resolveProvider)


// Format document on type options
struct lsDocumentOnTypeFormattingOptions :WorkDoneProgressOptions {
	// A character on which formatting should be triggered, like `}`.
	std::string firstTriggerCharacter;

	// More trigger characters.
	std::vector<std::string> moreTriggerCharacter;
	MAKE_SWAP_METHOD(lsDocumentOnTypeFormattingOptions, workDoneProgress,
		firstTriggerCharacter,
		moreTriggerCharacter);
};
MAKE_REFLECT_STRUCT(lsDocumentOnTypeFormattingOptions, workDoneProgress,
	firstTriggerCharacter,
	moreTriggerCharacter);
struct RenameOptions : WorkDoneProgressOptions {
	/**
	 * Renames should be checked and tested before being executed.
	 */
	optional<bool> prepareProvider;
	MAKE_SWAP_METHOD(RenameOptions, workDoneProgress, prepareProvider);
};
MAKE_REFLECT_STRUCT(RenameOptions,workDoneProgress,prepareProvider)

struct DocumentFilter{
	/**
	 * A language id, like `typescript`.
	 */
	optional<std::string> language;
	/**
	 * A uri scheme, like `file` or `untitled`.
	 */
	optional<std::string>scheme;
	/**
	 * A glob pattern, like `*.{ts,js}`.
	 */
	optional<std::string>pattern;
	MAKE_SWAP_METHOD(DocumentFilter, language, scheme, pattern)
};
MAKE_REFLECT_STRUCT(DocumentFilter,language,scheme,pattern)

// Document link options
struct lsDocumentLinkOptions :WorkDoneProgressOptions {
	// Document links have a resolve provider as well.
	optional<bool> resolveProvider;
	MAKE_SWAP_METHOD(lsDocumentLinkOptions, workDoneProgress, resolveProvider);
};
MAKE_REFLECT_STRUCT(lsDocumentLinkOptions, workDoneProgress,resolveProvider);

// Execute command options.
struct lsExecuteCommandOptions : WorkDoneProgressOptions {
	// The commands to be executed on the server
	std::vector<std::string> commands;
	MAKE_SWAP_METHOD(lsExecuteCommandOptions, workDoneProgress, commands);
};
MAKE_REFLECT_STRUCT(lsExecuteCommandOptions, workDoneProgress, commands);


struct TextDocumentRegistrationOptions
{
/**
 * A document selector to identify the scope of the registration. If set to null
 * the document selector provided on the client side will be used.
 */
	std::vector<DocumentFilter>documentSelector;

	MAKE_SWAP_METHOD(TextDocumentRegistrationOptions, documentSelector);
};
MAKE_REFLECT_STRUCT(TextDocumentRegistrationOptions, documentSelector);

struct StaticRegistrationOptions :public TextDocumentRegistrationOptions
{
	optional<std::string> id;
	MAKE_SWAP_METHOD(StaticRegistrationOptions, documentSelector, id);
};
MAKE_REFLECT_STRUCT(StaticRegistrationOptions, documentSelector,id);


struct lsServerCapabilities {
	// Defines how text documents are synced. Is either a detailed structure
	// defining each notification or for backwards compatibility the
	
	// TextDocumentSyncKind number.
	optional< std::pair<optional<lsTextDocumentSyncKind>, optional<lsTextDocumentSyncOptions> >> textDocumentSync;
	
	// The server provides hover support.
	optional<bool>  hoverProvider;
	
	// The server provides completion support.
	optional < lsCompletionOptions > completionProvider;
	
	// The server provides signature help support.
	optional < lsSignatureHelpOptions > signatureHelpProvider;
	
	// The server provides goto definition support.
	optional< std::pair< optional<bool>, optional<WorkDoneProgressOptions> > > definitionProvider;
	

  /**
   * The server provides Goto Type Definition support.
   *
   * Since 3.6.0
   */
	optional< std::pair< optional<bool>, optional<StaticRegistrationOptions> > > typeDefinitionProvider ;
	
	// The server provides implementation support.
	optional< std::pair< optional<bool>, optional<StaticRegistrationOptions> > >  implementationProvider ;
	
	// The server provides find references support.
	optional< std::pair< optional<bool>, optional<WorkDoneProgressOptions> > > referencesProvider ;
	
	// The server provides document highlight support.
	optional< std::pair< optional<bool>, optional<WorkDoneProgressOptions> > > documentHighlightProvider ;
	
	// The server provides document symbol support.
	optional< std::pair< optional<bool>, optional<WorkDoneProgressOptions> > > documentSymbolProvider ;
	
	// The server provides workspace symbol support.
	optional< std::pair< optional<bool>, optional<WorkDoneProgressOptions> > > workspaceSymbolProvider ;
	
	// The server provides code actions.
	optional< std::pair< optional<bool>, optional<CodeActionOptions> > > codeActionProvider ;
	
	// The server provides code lens.
	optional<CodeLensOptions> codeLensProvider;
	
	// The server provides document formatting.
	optional< std::pair< optional<bool>, optional<WorkDoneProgressOptions> > > documentFormattingProvider ;
	
	// The server provides document range formatting.
	optional< std::pair< optional<bool>, optional<WorkDoneProgressOptions> > > documentRangeFormattingProvider ;
	
	// The server provides document formatting on typing.
	optional<lsDocumentOnTypeFormattingOptions> documentOnTypeFormattingProvider;
	
	// The server provides rename support.
	optional< std::pair< optional<bool>, optional<RenameOptions> > >  renameProvider;

	
	// The server provides document link support.
	optional<lsDocumentLinkOptions > documentLinkProvider;
	
	// The server provides execute command support.
	optional < lsExecuteCommandOptions >executeCommandProvider;

	optional<lsp::Any> experimental;


	MAKE_SWAP_METHOD(lsServerCapabilities,
		textDocumentSync,
		hoverProvider,
		completionProvider,
		signatureHelpProvider,
		definitionProvider,
		typeDefinitionProvider,
		implementationProvider,
		referencesProvider,
		documentHighlightProvider,
		documentSymbolProvider,
		workspaceSymbolProvider,
		codeActionProvider,
		codeLensProvider,
		documentFormattingProvider,
		documentRangeFormattingProvider,
		documentOnTypeFormattingProvider,
		renameProvider,
		documentLinkProvider,
		executeCommandProvider, experimental);
	
};
MAKE_REFLECT_STRUCT(lsServerCapabilities,
	textDocumentSync,
	hoverProvider,
	completionProvider,
	signatureHelpProvider,
	definitionProvider,
	typeDefinitionProvider,
	implementationProvider,
	referencesProvider,
	documentHighlightProvider,
	documentSymbolProvider,
	workspaceSymbolProvider,
	codeActionProvider,
	codeLensProvider,
	documentFormattingProvider,
	documentRangeFormattingProvider,
	documentOnTypeFormattingProvider,
	renameProvider,
	documentLinkProvider,
	executeCommandProvider, experimental);


