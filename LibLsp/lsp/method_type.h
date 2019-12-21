#pragma once
#include <string>

using MethodType = const  char* const;


extern  MethodType kMethodType_Unknown;
extern  MethodType kMethodType_Exit;
extern  MethodType kMethodType_TextDocumentPublishDiagnostics;
extern  MethodType kMethodType_CqueryPublishInactiveRegions;
extern  MethodType kMethodType_CqueryQueryDbStatus;
extern  MethodType kMethodType_CqueryPublishSemanticHighlighting;


extern  MethodType kMethodType_initialize;
extern  MethodType kMethodType_shutdown;
extern  MethodType kMethodType_TextDocumentCodeAction;
extern  MethodType kMethodType_TextDocumentCodeLens ;
extern  MethodType kMethodType_TextDocumentComplete ;

extern  MethodType kMethodType_TextDocumentDefinition;
extern  MethodType kMethodType_TextDocumentDidChange ;
extern  MethodType kMethodType_TextDocumentDidClose ;
extern  MethodType kMethodType_TextDocumentDidOpen ;
extern  MethodType kMethodType_TextDocumentDidSave;
extern  MethodType kMethodType_TextDocumentDocumentHighlight;
extern  MethodType kMethodType_TextDocumentDocumentLink;
extern  MethodType kMethodType_TextDocumentDocumentSymbol;
extern  MethodType kMethodType_TextDocumentFormatting ;
extern  MethodType kMethodType_TextDocumentHover;
extern  MethodType kMethodType_TextDocumentImplementation;
extern  MethodType kMethodType_TextDocumentRangeFormatting;
extern  MethodType kMethodType_TextDocumentReferences;
extern  MethodType kMethodType_TextDocumentRename;
extern  MethodType kMethodType_TextDocumentSignatureHelp;
extern  MethodType kMethodType_TextDocumentTypeDefinition;
extern  MethodType kMethodType_WorkspaceDidChangeConfiguration;
extern  MethodType kMethodType_WorkspaceDidChangeWatchedFiles;
extern  MethodType kMethodType_WorkspaceExecuteCommand ;
extern  MethodType kMethodType_WorkspaceSymbol;


