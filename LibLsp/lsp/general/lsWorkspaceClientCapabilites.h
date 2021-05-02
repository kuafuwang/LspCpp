#pragma once
#include "LibLsp/lsp/method_type.h"


#include <stdexcept>

#include "LibLsp/JsonRpc/message.h"
#include "LibLsp/lsp/lsDocumentUri.h"
#include "LibLsp/lsp/lsAny.h"
#include "LibLsp/lsp/extention/jdtls/searchSymbols.h"

/**
 * Capabilities specific to `WorkspaceEdit`s
 */

struct WorkspaceEditCapabilities {
	/**
	 * The client supports versioned document changes in `WorkspaceEdit`s
	 */
	optional<bool>  documentChanges;

	/**
	 * The client supports resource changes
	 * in `WorkspaceEdit`s.
	 *
	 * @deprecated Since LSP introduces resource operations, use {link #resourceOperations}
	 */

	optional<bool> resourceChanges;

	/**
	 * The resource operations the client supports. Clients should at least
	 * support 'create', 'rename' and 'delete' files and folders.
	 *
	 * See {@link ResourceOperationKind} for allowed values.
	 */
	optional< std::vector<std::string> > resourceOperations;

	/**
	 * The failure handling strategy of a client if applying the workspace edit
	 * fails.
	 *
	 * See {@link FailureHandlingKind} for allowed values.
	 */
	optional<std::string > failureHandling;

	MAKE_SWAP_METHOD(WorkspaceEditCapabilities, documentChanges, resourceChanges, resourceOperations, failureHandling)

};
MAKE_REFLECT_STRUCT(WorkspaceEditCapabilities,documentChanges, resourceChanges, resourceOperations, failureHandling)


struct DynamicRegistrationCapabilities {
	// Did foo notification supports dynamic registration.
	optional<bool> dynamicRegistration;

	MAKE_SWAP_METHOD(DynamicRegistrationCapabilities,
		dynamicRegistration);
};

MAKE_REFLECT_STRUCT(DynamicRegistrationCapabilities,
	dynamicRegistration);



// Workspace specific client capabilities.
struct SymbolKindCapabilities
{
	optional< std::vector<lsSymbolKind> >  valueSet;

	MAKE_SWAP_METHOD(SymbolKindCapabilities, valueSet)


};
MAKE_REFLECT_STRUCT(SymbolKindCapabilities, valueSet)




struct SymbolCapabilities :public DynamicRegistrationCapabilities {
	/**
	 * Specific capabilities for the `SymbolKind` in the `workspace/symbol` request.
	 */
	optional<SymbolKindCapabilities>  symbolKind;

	MAKE_SWAP_METHOD(SymbolCapabilities,
		symbolKind, dynamicRegistration);
};
MAKE_REFLECT_STRUCT(SymbolCapabilities,
	symbolKind, dynamicRegistration);




struct lsWorkspaceClientCapabilites {
  // The client supports applying batch edits to the workspace.
  optional<bool> applyEdit;

 

  // Capabilities specific to `WorkspaceEdit`s
  optional<WorkspaceEditCapabilities> workspaceEdit;



  // Capabilities specific to the `workspace/didChangeConfiguration`
  // notification.
  optional<DynamicRegistrationCapabilities> didChangeConfiguration;

  // Capabilities specific to the `workspace/didChangeWatchedFiles`
  // notification.
  optional<DynamicRegistrationCapabilities> didChangeWatchedFiles;

  // Capabilities specific to the `workspace/symbol` request.
  optional<SymbolCapabilities> symbol;

  // Capabilities specific to the `workspace/executeCommand` request.
  optional<DynamicRegistrationCapabilities> executeCommand;


  /**
 * The client has support for workspace folders.
 *
 * Since 3.6.0
 */
  optional<bool> workspaceFolders;

  /**
   * The client supports `workspace/configuration` requests.
   *
   * Since 3.6.0
   */
  optional<bool> configuration;

  MAKE_SWAP_METHOD(lsWorkspaceClientCapabilites,
	  applyEdit,
	  workspaceEdit,
	  didChangeConfiguration,
	  didChangeWatchedFiles,
	  symbol,
	  executeCommand, workspaceFolders, configuration);
};

MAKE_REFLECT_STRUCT(lsWorkspaceClientCapabilites,
                    applyEdit,
                    workspaceEdit,
                    didChangeConfiguration,
                    didChangeWatchedFiles,
                    symbol,
                    executeCommand,workspaceFolders, configuration);




