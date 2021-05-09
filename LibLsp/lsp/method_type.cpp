
#include "method_type.h"
#include "general/exit.h"
#include "general/initialize.h"
#include "general/shutdown.h"
#include "textDocument/code_action.h"
#include "textDocument/code_lens.h"
#include "textDocument/completion.h"

#include "textDocument/did_change.h"
#include "textDocument/did_close.h"
#include "textDocument/did_open.h"
#include "textDocument/did_save.h"
#include "textDocument/document_link.h"
#include "textDocument/document_symbol.h"
#include "textDocument/formatting.h"
#include "textDocument/highlight.h"
#include "textDocument/hover.h"
#include "textDocument/implementation.h"
#include "textDocument/range_formatting.h"
#include "textDocument/references.h"
#include "textDocument/rename.h"
#include "textDocument/signature_help.h"
#include "textDocument/type_definition.h"
#include "workspace/did_change_configuration.h"
#include "workspace/did_change_watched_files.h"
#include "workspace/symbol.h"
#include "LibLsp/JsonRpc/cancellation.h"
#include "textDocument/typeHierarchy.h"
#include "JavaExtentions/searchSymbols.h"
#include "JavaExtentions/executeCommand.h"
#include "workspace/applyEdit.h"
#include "workspace/configuration.h"
#include "client/unregisterCapability.h"
#include "client/registerCapability.h"
#include "textDocument/publishDiagnostics.h"

#include "workspace/didChangeWorkspaceFolders.h"
#include "textDocument/declaration_definition.h"
#include "JavaExtentions/classFileContents.h"
#include "JavaExtentions/projectConfigurationUpdate.h"
#include "JavaExtentions/listOverridableMethods.h"
#include "JavaExtentions/generateToString.h"
#include "JavaExtentions/addOverridableMethods.h"
#include "JavaExtentions/generateHashCodeEquals.h"
#include "JavaExtentions/organizeImports.h"
#include "JavaExtentions/checkToStringStatus.h"
#include "JavaExtentions/resolveUnimplementedAccessors.h"
#include "JavaExtentions/generateAccessors.h"
#include "JavaExtentions/checkConstructorsStatus.h"
#include "JavaExtentions/checkDelegateMethodsStatus.h"
#include "JavaExtentions/generateDelegateMethods.h"
#include "JavaExtentions/getRefactorEdit.h"
#include "JavaExtentions/getMoveDestinations.h"
#include "JavaExtentions/Move.h"
#include "JavaExtentions/findLinks.h"
#include "textDocument/resolveCompletionItem.h"
#include "textDocument/resolveCodeLens.h"
#include "textDocument/onTypeFormatting.h"
#include "textDocument/willSave.h"
#include "textDocument/colorPresentation.h"
#include "textDocument/foldingRange.h"
#include "textDocument/prepareRename.h"
#include "textDocument/resolveTypeHierarchy.h"
#include "textDocument/callHierarchy.h"
#include "textDocument/selectionRange.h"
#include "JavaExtentions/buildWorkspace.h"
#include "JavaExtentions/generateConstructors.h"
#include "general/initialized.h"
#include "windows/MessageNotify.h"
#include "language/language.h"
#include "textDocument/didRenameFiles.h"
#include "textDocument/semanticHighlighting.h"
 MethodType kMethodType_Unknown = "$unknown";

 MethodType kMethodType_Exit = "exit";
 MethodType Notify_Exit::kMethodType = "exit";

 MethodType kMethodType_TextDocumentPublishDiagnostics =
    "textDocument/publishDiagnostics";

 MethodType kMethodType_CqueryPublishInactiveRegions =
    "$cquery/publishInactiveRegions";
 MethodType kMethodType_CqueryQueryDbStatus = "$cquery/queryDbStatus";
 MethodType kMethodType_CqueryPublishSemanticHighlighting =
    "$cquery/publishSemanticHighlighting";

 MethodType kMethodType_initialize = "initialize";

 MethodType td_initialize::kMethodType = "initialize";
 MethodType Notify_InitializedNotification::kMethodType = "initialized";

 MethodType kMethodType_shutdown = "shutdown";

 MethodType td_shutdown::kMethodType = "shutdown";

 MethodType kMethodType_TextDocumentCodeAction = "textDocument/codeAction";
 MethodType td_codeAction::kMethodType = "textDocument/codeAction";

 MethodType kMethodType_TextDocumentCodeLens = "textDocument/codeLens";
 MethodType td_codeLens::kMethodType = "textDocument/codeLens";

 MethodType kMethodType_TextDocumentComplete = "textDocument/completion";
 MethodType td_completion::kMethodType = "textDocument/completion";


 MethodType kMethodType_TextDocumentDefinition = "textDocument/definition";

 MethodType td_definition::kMethodType = "textDocument/definition";

 MethodType td_declaration::kMethodType = "textDocument/declaration";


 MethodType kMethodType_TextDocumentDidChange = "textDocument/didChange";
 MethodType Notify_TextDocumentDidChange::kMethodType = "textDocument/didChange";


 MethodType kMethodType_TextDocumentDidClose = "textDocument/didClose";
 MethodType Notify_TextDocumentDidClose::kMethodType = "textDocument/didClose";

 MethodType kMethodType_TextDocumentDidOpen = "textDocument/didOpen";
 MethodType Notify_TextDocumentDidOpen::kMethodType = "textDocument/didOpen";

 MethodType td_didRenameFiles::kMethodType = "java/didRenameFiles";
 MethodType td_willRenameFiles::kMethodType = "java/willRenameFiles";
 

 MethodType kMethodType_TextDocumentDidSave = "textDocument/didSave";
 MethodType Notify_TextDocumentDidSave::kMethodType = "textDocument/didSave";

 MethodType td_willSave::kMethodType = "textDocument/willSave";

 MethodType td_willSaveWaitUntil::kMethodType = "textDocument/willSaveWaitUntil";

 MethodType td_linkResolve::kMethodType = "documentLink/resolve";

 MethodType td_colorPresentation::kMethodType = "textDocument/colorPresentation";

 MethodType td_documentColor::kMethodType = "textDocument/documentColor";

 MethodType td_foldingRange::kMethodType = "textDocument/foldingRange";

 MethodType td_prepareRename::kMethodType = "textDocument/prepareRename";



 MethodType kMethodType_TextDocumentDocumentLink = "textDocument/documentLink";
 MethodType td_links::kMethodType = "textDocument/documentLink";


 MethodType kMethodType_TextDocumentDocumentSymbol = "textDocument/documentSymbol";
 MethodType td_symbol::kMethodType = "textDocument/documentSymbol";


 MethodType kMethodType_TextDocumentFormatting = "textDocument/formatting";
 MethodType td_formatting::kMethodType = "textDocument/formatting";


 MethodType kMethodType_TextDocumentDocumentHighlight = "textDocument/documentHighlight";
 MethodType td_highlight::kMethodType = "textDocument/documentHighlight";


 MethodType kMethodType_TextDocumentHover = "textDocument/hover";
 MethodType td_hover::kMethodType = "textDocument/hover";


 MethodType kMethodType_TextDocumentImplementation = "textDocument/implementation";
 MethodType td_implementation::kMethodType = "textDocument/implementation";

 MethodType kMethodType_TextDocumentRangeFormatting = "textDocument/rangeFormatting";
 MethodType td_rangeFormatting::kMethodType = "textDocument/rangeFormatting";

 MethodType kMethodType_TextDocumentReferences = "textDocument/references";
 MethodType td_references::kMethodType = "textDocument/references";

 MethodType kMethodType_TextDocumentRename = "textDocument/rename";
 MethodType td_rename::kMethodType = "textDocument/rename";

 MethodType kMethodType_TextDocumentSignatureHelp = "textDocument/signatureHelp";
 MethodType td_signatureHelp::kMethodType = "textDocument/signatureHelp";

 MethodType kMethodType_TextDocumentTypeDefinition = "textDocument/typeDefinition";

 MethodType td_typeDefinition::kMethodType = "textDocument/typeDefinition";

 MethodType codeLens_resolve::kMethodType = "codeLens/resolve";

 MethodType td_onTypeFormatting::kMethodType = "textDocument/onTypeFormatting";

 MethodType typeHierarchy_resolve::kMethodType = "typeHierarchy/resolve";

 MethodType td_callHierarchy::kMethodType = "textDocument/callHierarchy";

 MethodType td_selectionRange::kMethodType = "textDocument/selectionRange";

 MethodType kMethodType_WorkspaceDidChangeConfiguration = "workspace/didChangeConfiguration";

 MethodType Notify_WorkspaceDidChangeConfiguration::kMethodType = "workspace/didChangeConfiguration";

 MethodType kMethodType_WorkspaceDidChangeWatchedFiles = "workspace/didChangeWatchedFiles";

 MethodType Notify_WorkspaceDidChangeWatchedFiles::kMethodType = "workspace/didChangeWatchedFiles";

 MethodType kMethodType_WorkspaceExecuteCommand = "workspace/executeCommand";

 MethodType wp_executeCommand::kMethodType = "workspace/executeCommand";

 MethodType kMethodType_WorkspaceSymbol = "workspace/symbol";

 MethodType wp_symbol::kMethodType = "workspace/symbol";

 MethodType Notify_sendNotification::kMethodType = "workspace/notify";

 MethodType kMethodType_Cancellation = "$/cancelRequest";

 MethodType Notify_Cancellation::kMethodType = "$/cancelRequest";

 MethodType td_typeHierarchy::kMethodType = "textDocument/typeHierarchy";


 MethodType Notify_TextDocumentPublishDiagnostics::kMethodType = "textDocument/publishDiagnostics";

 MethodType Notify_LogMessage::kMethodType = "window/logMessage";

 MethodType Notify_ShowMessage::kMethodType = "window/showMessage";

 MethodType WorkspaceApply::kMethodType = "workspace/applyEdit";

 MethodType WorkspaceConfiguration::kMethodType = "workspace/configuration";

 MethodType Req_ClientUnregisterCapability::kMethodType = "client/unregisterCapability";

 MethodType Req_ClientRegisterCapability::kMethodType = "client/registerCapability";

 MethodType WindowShowMessage::kMethodType = "window/showMessage";





 MethodType java_classFileContents::kMethodType = "java/classFileContents";

 MethodType java_searchSymbols::kMethodType = "java/searchSymbols";

 MethodType java_executeCommand::kMethodType = "java/executeCommand";

 MethodType java_projectConfigurationUpdate::kMethodType = "java/projectConfigurationUpdate";

 MethodType java_listOverridableMethods::kMethodType = "java/listOverridableMethods";

 MethodType java_addOverridableMethods::kMethodType = "java/addOverridableMethods";

 MethodType java_checkHashCodeEqualsStatus::kMethodType = "java/checkHashCodeEqualsStatus";

 MethodType java_generateHashCodeEquals::kMethodType = "java/generateHashCodeEquals";

 MethodType java_organizeImports::kMethodType = "java/organizeImports";

 MethodType java_checkToStringStatus::kMethodType = "java/checkToStringStatus";

 MethodType java_generateToString::kMethodType = "java/generateToString";

 MethodType java_resolveUnimplementedAccessors::kMethodType = "java/resolveUnimplementedAccessors";

 MethodType completionItem_resolve::kMethodType = "completionItem/resolve";
		   

 MethodType java_generateAccessors::kMethodType = "java/generateAccessors";

 MethodType java_checkConstructorsStatus::kMethodType = "java/checkConstructorsStatus";

 MethodType java_checkDelegateMethodsStatus::kMethodType = "java/checkDelegateMethodsStatus";

 MethodType java_generateDelegateMethods::kMethodType = "java/generateDelegateMethods";

 MethodType java_getRefactorEdit::kMethodType = "java/getRefactorEdit";

 MethodType java_getMoveDestinations::kMethodType = "java/getMoveDestinations";

 MethodType java_move::kMethodType = "java/move";

 MethodType java_findLinks::kMethodType = "java/findLinks";


 MethodType java_buildWorkspace::kMethodType = "java/buildWorkspace";

 MethodType java_generateConstructors::kMethodType = "java/generateConstructors";



 MethodType WorkspaceFolders::kMethodType = "workspace/workspaceFolders";

 MethodType Notify_WorkspaceDidChangeWorkspaceFolders::kMethodType = "workspace/didChangeWorkspaceFolders";

 MethodType Notify_semanticHighlighting::kMethodType = "textDocument/semanticHighlighting";

 MethodType lang_status::kMethodType = "language/status";

 MethodType lang_actionableNotification::kMethodType = "language/actionableNotification";

 MethodType lang_progressReport::kMethodType = "language/progressReport";

 MethodType lang_eventNotification::kMethodType = "language/eventNotification";
 