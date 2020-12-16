#include "ProtocolJsonHandler.h"
#include "general/initialize.h"
#include "general/shutdown.h"
#include "textDocument/code_action.h"
#include "textDocument/code_lens.h"
#include "textDocument/completion.h"


#include "textDocument/did_close.h"

#include "textDocument/highlight.h"
#include "textDocument/document_link.h"
#include "textDocument/formatting.h"
#include "textDocument/hover.h"
#include "textDocument/implementation.h"
#include "textDocument/range_formatting.h"
#include "textDocument/references.h"
#include "textDocument/rename.h"
#include "textDocument/signature_help.h"
#include "textDocument/type_definition.h"
#include "workspace/symbol.h"
#include "textDocument/typeHierarchy.h"
#include "out_list.h"
#include "JavaExtentions/codeActionResult.h"
#include "textDocument/declaration_definition.h"
#include "textDocument/resolveCompletionItem.h"
#include "textDocument/resolveCodeLens.h"
#include "textDocument/colorPresentation.h"
#include "textDocument/foldingRange.h"
#include "textDocument/prepareRename.h"
#include "textDocument/resolveTypeHierarchy.h"
#include "textDocument/callHierarchy.h"
#include "textDocument/selectionRange.h"
#include "JavaExtentions/classFileContents.h"
#include "JavaExtentions/buildWorkspace.h"
#include "JavaExtentions/listOverridableMethods.h"
#include "JavaExtentions/addOverridableMethods.h"
#include "JavaExtentions/checkHashCodeEqualsStatus.h"
#include "JavaExtentions/checkConstructorsStatus.h"
#include "JavaExtentions/checkDelegateMethodsStatus.h"
#include "JavaExtentions/checkToStringStatus.h"
#include "JavaExtentions/executeCommand.h"
#include "JavaExtentions/findLinks.h"
#include "JavaExtentions/generateAccessors.h"
#include "JavaExtentions/generateConstructors.h"
#include "JavaExtentions/generateDelegateMethods.h"
#include "JavaExtentions/generateHashCodeEquals.h"
#include "JavaExtentions/generateToString.h"
#include "JavaExtentions/getMoveDestinations.h"
#include "JavaExtentions/Move.h"
#include "JavaExtentions/organizeImports.h"
#include "general/exit.h"
#include "general/initialized.h"
#include "JavaExtentions/projectConfigurationUpdate.h"
#include "textDocument/did_change.h"
#include "textDocument/did_open.h"
#include "textDocument/did_save.h"
#include "textDocument/publishDiagnostics.h"
#include "textDocument/willSave.h"

#include "workspace/didChangeWorkspaceFolders.h"
#include "workspace/did_change_configuration.h"
#include "workspace/did_change_watched_files.h"
#include "windows/MessageNotify.h"
#include "language/language.h"
#include "client/registerCapability.h"
#include "client/unregisterCapability.h"
#include "textDocument/didRenameFiles.h"
#include "textDocument/semanticHighlighting.h"
#include "workspace/configuration.h"


void AddStadardResponseJsonRpcMethod(MessageJsonHandler& handler)
{
	
	handler.method2response[td_initialize::kMethodType] = [](Reader& visitor)
	{
		if(visitor.HasMember("error"))
		 return 	Rsp_Error::ReflectReader(visitor);
		
		return td_initialize::response::ReflectReader(visitor);
	};
	
	handler.method2response[td_shutdown::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_shutdown::response::ReflectReader(visitor);
	};
	handler.method2response[td_codeAction::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_codeAction::response::ReflectReader(visitor);
	};
	handler.method2response[td_codeLens::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_codeLens::response::ReflectReader(visitor);
	};
	handler.method2response[td_completion::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_completion::response::ReflectReader(visitor);
	};

	handler.method2response[td_definition::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_definition::response::ReflectReader(visitor);
	};
	handler.method2response[td_declaration::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_declaration::response::ReflectReader(visitor);
	};
	handler.method2response[td_willSaveWaitUntil::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_willSaveWaitUntil::response::ReflectReader(visitor);
	};
	
	handler.method2response[td_highlight::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_highlight::response::ReflectReader(visitor);
	};
	
	handler.method2response[td_links::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_links::response::ReflectReader(visitor);
	};
	
	handler.method2response[td_linkResolve::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_linkResolve::response::ReflectReader(visitor);
	};
	
	handler.method2response[td_symbol::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_symbol::response::ReflectReader(visitor);
	};
	
	handler.method2response[td_formatting::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_formatting::response::ReflectReader(visitor);
	};

	handler.method2response[td_hover::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return Rsp_TextDocumentHover::ReflectReader(visitor);
	
	};
	
	handler.method2response[td_implementation::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_implementation::response::ReflectReader(visitor);
	};

	handler.method2response[td_rangeFormatting::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_rangeFormatting::response::ReflectReader(visitor);
	};

	handler.method2response[td_references::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_references::response::ReflectReader(visitor);
	};
	
	handler.method2response[td_rename::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_rename::response::ReflectReader(visitor);
	};


	handler.method2response[td_signatureHelp::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_signatureHelp::response::ReflectReader(visitor);
	};

	handler.method2response[td_typeDefinition::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_typeDefinition::response::ReflectReader(visitor);
	};

	handler.method2response[wp_executeCommand::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return wp_executeCommand::response::ReflectReader(visitor);
	};

	handler.method2response[wp_symbol::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return wp_symbol::response::ReflectReader(visitor);
	};
	handler.method2response[td_typeHierarchy::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_typeHierarchy::response::ReflectReader(visitor);
	};
	handler.method2response[completionItem_resolve::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return completionItem_resolve::response::ReflectReader(visitor);
	};

	handler.method2response[codeLens_resolve::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		
		return codeLens_resolve::response::ReflectReader(visitor);
		
	};

	handler.method2response[td_colorPresentation::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_colorPresentation::response::ReflectReader(visitor);

	};
	handler.method2response[td_documentColor::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_documentColor::response::ReflectReader(visitor);

	};
	handler.method2response[td_foldingRange::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_foldingRange::response::ReflectReader(visitor);

	};
	handler.method2response[td_prepareRename::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_prepareRename::response::ReflectReader(visitor);

	};
	handler.method2response[typeHierarchy_resolve::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return typeHierarchy_resolve::response::ReflectReader(visitor);

	};
	handler.method2response[td_callHierarchy::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_callHierarchy::response::ReflectReader(visitor);

	};
	handler.method2response[td_selectionRange::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_selectionRange::response::ReflectReader(visitor);

	};
	handler.method2response[td_didRenameFiles::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_didRenameFiles::response::ReflectReader(visitor);

	};
	handler.method2response[td_willRenameFiles::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return td_willRenameFiles::response::ReflectReader(visitor);

	};
	
}


void AddJavaExtentionResponseJsonRpcMethod(MessageJsonHandler& handler)
{
	handler.method2response[java_classFileContents::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_classFileContents::response::ReflectReader(visitor);
	};
	handler.method2response[java_buildWorkspace::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_buildWorkspace::response::ReflectReader(visitor);
	};
	handler.method2response[java_listOverridableMethods::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_listOverridableMethods::response::ReflectReader(visitor);
	};
	handler.method2response[java_listOverridableMethods::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_listOverridableMethods::response::ReflectReader(visitor);
	};

	handler.method2response[java_checkHashCodeEqualsStatus::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_checkHashCodeEqualsStatus::response::ReflectReader(visitor);
	};


	handler.method2response[java_addOverridableMethods::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_addOverridableMethods::response::ReflectReader(visitor);
	};

	handler.method2response[java_checkConstructorsStatus::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_checkConstructorsStatus::response::ReflectReader(visitor);
	};


	handler.method2response[java_checkDelegateMethodsStatus::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_checkDelegateMethodsStatus::response::ReflectReader(visitor);
	};
	handler.method2response[java_checkToStringStatus::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_checkToStringStatus::response::ReflectReader(visitor);
	};


	handler.method2response[java_generateAccessors::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_generateAccessors::response::ReflectReader(visitor);
	};
	handler.method2response[java_generateConstructors::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_generateConstructors::response::ReflectReader(visitor);
	};
	handler.method2response[java_generateDelegateMethods::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_generateDelegateMethods::response::ReflectReader(visitor);
	};

	handler.method2response[java_generateHashCodeEquals::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_generateHashCodeEquals::response::ReflectReader(visitor);
	};
	handler.method2response[java_generateToString::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_generateToString::response::ReflectReader(visitor);
	};

	handler.method2response[java_generateToString::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_generateToString::response::ReflectReader(visitor);
	};

	handler.method2response[java_getMoveDestinations::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_getMoveDestinations::response::ReflectReader(visitor);
	};

	handler.method2response[java_getRefactorEdit::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_getRefactorEdit::response::ReflectReader(visitor);
	};

	handler.method2response[java_move::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_move::response ::ReflectReader(visitor);
	};

	handler.method2response[java_organizeImports::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_organizeImports::response::ReflectReader(visitor);
	};

	handler.method2response[java_resolveUnimplementedAccessors::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_resolveUnimplementedAccessors::response::ReflectReader(visitor);
	};

	handler.method2response[java_searchSymbols::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);

		return java_searchSymbols::response::ReflectReader(visitor);
	};

	handler.method2request[WorkspaceConfiguration::kMethodType] = [](Reader& visitor)
	{
		return WorkspaceConfiguration::request::ReflectReader(visitor);
	};
	handler.method2request[WorkspaceFolders::kMethodType] = [](Reader& visitor)
	{
		return WorkspaceFolders::request::ReflectReader(visitor);
	};
	
}

void AddNotifyJsonRpcMethod(MessageJsonHandler& handler)
{
	handler.method2notification[Notify_Exit::kMethodType] = [](Reader& visitor)
	{
		return Notify_Exit::notify::ReflectReader(visitor);
	};
	handler.method2notification[Notify_InitializedNotification::kMethodType] = [](Reader& visitor)
	{
		return Notify_InitializedNotification::notify::ReflectReader(visitor);
	};

	handler.method2notification[java_projectConfigurationUpdate::kMethodType] = [](Reader& visitor)
	{
		return java_projectConfigurationUpdate::notify::ReflectReader(visitor);
	};

	handler.method2notification[Notify_TextDocumentDidChange::kMethodType] = [](Reader& visitor)
	{
		return Notify_TextDocumentDidChange::notify::ReflectReader(visitor);
	};

	handler.method2notification[Notify_TextDocumentDidClose::kMethodType] = [](Reader& visitor)
	{
		return Notify_TextDocumentDidClose::notify::ReflectReader(visitor);
	};


	handler.method2notification[Notify_TextDocumentDidOpen::kMethodType] = [](Reader& visitor)
	{
		return Notify_TextDocumentDidOpen::notify::ReflectReader(visitor);
	};

	handler.method2notification[Notify_TextDocumentDidSave::kMethodType] = [](Reader& visitor)
	{
		return Notify_TextDocumentDidSave::notify::ReflectReader(visitor);
	};

	handler.method2notification[Notify_TextDocumentPublishDiagnostics::kMethodType] = [](Reader& visitor)
	{
		return Notify_TextDocumentPublishDiagnostics::notify::ReflectReader(visitor);
	};
	handler.method2notification[Notify_semanticHighlighting::kMethodType] = [](Reader& visitor)
	{
		return Notify_semanticHighlighting::notify::ReflectReader(visitor);
	};
	handler.method2notification[td_willSave::kMethodType] = [](Reader& visitor)
	{
		return td_willSave::notify::ReflectReader(visitor);
	};

	handler.method2notification[Notify_LogMessage::kMethodType] = [](Reader& visitor)
	{
		return Notify_LogMessage::notify::ReflectReader(visitor);
	};
	handler.method2notification[Notify_ShowMessage::kMethodType] = [](Reader& visitor)
	{
		return Notify_ShowMessage::notify::ReflectReader(visitor);
	};
	handler.method2notification[Notify_WorkspaceDidChangeWorkspaceFolders::kMethodType] = [](Reader& visitor)
	{
		return Notify_WorkspaceDidChangeWorkspaceFolders::notify::ReflectReader(visitor);
	};

	handler.method2notification[Notify_WorkspaceDidChangeConfiguration::kMethodType] = [](Reader& visitor)
	{
		return Notify_WorkspaceDidChangeConfiguration::notify::ReflectReader(visitor);
	};


	handler.method2notification[Notify_WorkspaceDidChangeWatchedFiles::kMethodType] = [](Reader& visitor)
	{
		return Notify_WorkspaceDidChangeWatchedFiles::notify::ReflectReader(visitor);
	};

	handler.method2notification[Notify_sendNotification::kMethodType] = [](Reader& visitor)
	{
		return Notify_sendNotification::notify::ReflectReader(visitor);
	};
	handler.method2notification[lang_status::kMethodType] = [](Reader& visitor)
	{
		return lang_status::notify::ReflectReader(visitor);
	};
	handler.method2notification[lang_actionableNotification::kMethodType] = [](Reader& visitor)
	{
		return lang_actionableNotification::notify::ReflectReader(visitor);
	};
	handler.method2notification[lang_progressReport::kMethodType] = [](Reader& visitor)
	{
		return lang_progressReport::notify::ReflectReader(visitor);
	};
	handler.method2notification[lang_eventNotification::kMethodType] = [](Reader& visitor)
	{
		return lang_eventNotification::notify::ReflectReader(visitor);
	};
}

void AddRequstJsonRpcMethod(MessageJsonHandler& handler)
{
	handler.method2request[Req_ClientRegisterCapability::kMethodType]= [](Reader& visitor)
	{

		return Req_ClientRegisterCapability::request::ReflectReader(visitor);
	};
	handler.method2request[Req_ClientUnregisterCapability::kMethodType] = [](Reader& visitor)
	{

		return Req_ClientUnregisterCapability::request::ReflectReader(visitor);
	};
}

void AddStandardRequestJsonRpcMethod(MessageJsonHandler& handler)
{

	handler.method2request[td_initialize::kMethodType] = [](Reader& visitor)
	{
	
		return td_initialize::request::ReflectReader(visitor);
	};
	handler.method2request[td_shutdown::kMethodType] = [](Reader& visitor)
	{

		return td_shutdown::request::ReflectReader(visitor);
	};
	handler.method2request[td_codeAction::kMethodType] = [](Reader& visitor)
	{


		return td_codeAction::request::ReflectReader(visitor);
	};
	handler.method2request[td_codeLens::kMethodType] = [](Reader& visitor)
	{

		return td_codeLens::request::ReflectReader(visitor);
	};
	handler.method2request[td_completion::kMethodType] = [](Reader& visitor)
	{

		return td_completion::request::ReflectReader(visitor);
	};

	handler.method2request[td_definition::kMethodType] = [](Reader& visitor)
	{

		return td_definition::request::ReflectReader(visitor);
	};
	handler.method2request[td_declaration::kMethodType] = [](Reader& visitor)
	{

		return td_declaration::request::ReflectReader(visitor);
	};
	handler.method2request[td_willSaveWaitUntil::kMethodType] = [](Reader& visitor)
	{
		if (visitor.HasMember("error"))
			return 	Rsp_Error::ReflectReader(visitor);
		return td_willSaveWaitUntil::request::ReflectReader(visitor);
	};

	handler.method2request[td_highlight::kMethodType] = [](Reader& visitor)
	{

		return td_highlight::request::ReflectReader(visitor);
	};

	handler.method2request[td_links::kMethodType] = [](Reader& visitor)
	{

		return td_links::request::ReflectReader(visitor);
	};

	handler.method2request[td_linkResolve::kMethodType] = [](Reader& visitor)
	{
	
		return td_linkResolve::request::ReflectReader(visitor);
	};

	handler.method2request[td_symbol::kMethodType] = [](Reader& visitor)
	{

		return td_symbol::request::ReflectReader(visitor);
	};

	handler.method2request[td_formatting::kMethodType] = [](Reader& visitor)
	{

		return td_formatting::request::ReflectReader(visitor);
	};

	handler.method2request[td_hover::kMethodType] = [](Reader& visitor)
	{
		return td_hover::request::ReflectReader(visitor);
	};

	handler.method2request[td_implementation::kMethodType] = [](Reader& visitor)
	{
	
		return td_implementation::request::ReflectReader(visitor);
	};
	
	handler.method2request[td_didRenameFiles::kMethodType] = [](Reader& visitor)
	{

		return td_didRenameFiles::request::ReflectReader(visitor);
	};
	
	handler.method2request[td_willRenameFiles::kMethodType] = [](Reader& visitor)
	{
		return td_willRenameFiles::request::ReflectReader(visitor);
	};
}


lsp::ProtocolJsonHandler::ProtocolJsonHandler()
{
	AddStadardResponseJsonRpcMethod(*this);
	AddJavaExtentionResponseJsonRpcMethod(*this);
	AddNotifyJsonRpcMethod(*this);
	AddStandardRequestJsonRpcMethod(*this);
	AddRequstJsonRpcMethod(*this);
}
