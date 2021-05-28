#pragma once

#include "completion.h"
#include "document_symbol.h"
#include "LibLsp/lsp/lsMarkedString.h"
#include "hover.h"
#include "prepareRename.h"
#include <LibLsp/lsp/textDocument/typeHierarchy.h>

void Reflect(Reader& visitor, TextDocumentComplete::Either& value)
{
	if(visitor.IsArray())
	{
		Reflect(visitor, value.first);
	}
	else
	{
	
		Reflect(visitor, value.second);
	}
	
}
void Reflect(Reader& visitor, TextDocumentDocumentSymbol::Either& value)
{
	if (visitor.HasMember("location"))
	{
		Reflect(visitor, value.first);
	}
	else
	{
		Reflect(visitor, value.second);
	}
}

void Reflect(Reader& visitor, std::pair<boost::optional<std::string>, boost::optional<lsMarkedString>>& value)
{
	
	if (!visitor.IsString())
	{
		Reflect(visitor, value.second);
	}
	else
	{
		Reflect(visitor, value.first);
	}
}

void Reflect(Reader& visitor, std::pair<boost::optional<std::string>, boost::optional<MarkupContent>>& value)
{
	if (!visitor.IsString())
	{
		Reflect(visitor, value.second);
	}
	else
	{
		Reflect(visitor, value.first);
	}
}
  void Reflect(Reader& visitor, TextDocumentHover::Either& value)
{
	  JsonReader& reader = dynamic_cast<JsonReader&>(visitor);
	  if (reader.IsArray())
	  {
		  Reflect(visitor, value.first);
	  }
	  else if(reader.m_->IsObject())
	  {
		  Reflect(visitor, value.second);
	  }
}

   void  Reflect(Reader& visitor, TextDocumentPrepareRenameResult& value)
{
	  if (visitor.HasMember("placeholder"))
	  {
		  Reflect(visitor, value.second);
	  }
	  else
	  {
		  Reflect(visitor, value.first);
	  }
}

  namespace
	  RefactorProposalUtility
  {
	    const char* APPLY_REFACTORING_COMMAND_ID = "java.action.applyRefactoringCommand";
	    const char* EXTRACT_VARIABLE_ALL_OCCURRENCE_COMMAND = "extractVariableAllOccurrence";
	    const char* EXTRACT_VARIABLE_COMMAND = "extractVariable";
	    const char* EXTRACT_CONSTANT_COMMAND = "extractConstant";
	    const char* EXTRACT_METHOD_COMMAND = "extractMethod";
	    const char* EXTRACT_FIELD_COMMAND = "extractField";
	    const char* CONVERT_VARIABLE_TO_FIELD_COMMAND = "convertVariableToField";
	    const char* MOVE_FILE_COMMAND = "moveFile";
	    const char* MOVE_INSTANCE_METHOD_COMMAND = "moveInstanceMethod";
	    const char* MOVE_STATIC_MEMBER_COMMAND = "moveStaticMember";
	    const char* MOVE_TYPE_COMMAND = "moveType";
  };
  namespace  QuickAssistProcessor {

	   const char* SPLIT_JOIN_VARIABLE_DECLARATION_ID = "org.eclipse.jdt.ls.correction.splitJoinVariableDeclaration.assist"; //$NON-NLS-1$
	   const char* CONVERT_FOR_LOOP_ID = "org.eclipse.jdt.ls.correction.convertForLoop.assist"; //$NON-NLS-1$
	   const char* ASSIGN_TO_LOCAL_ID = "org.eclipse.jdt.ls.correction.assignToLocal.assist"; //$NON-NLS-1$
	   const char* ASSIGN_TO_FIELD_ID = "org.eclipse.jdt.ls.correction.assignToField.assist"; //$NON-NLS-1$
	   const char* ASSIGN_PARAM_TO_FIELD_ID = "org.eclipse.jdt.ls.correction.assignParamToField.assist"; //$NON-NLS-1$
	   const char* ASSIGN_ALL_PARAMS_TO_NEW_FIELDS_ID = "org.eclipse.jdt.ls.correction.assignAllParamsToNewFields.assist"; //$NON-NLS-1$
	   const char* ADD_BLOCK_ID = "org.eclipse.jdt.ls.correction.addBlock.assist"; //$NON-NLS-1$
	   const char* EXTRACT_LOCAL_ID = "org.eclipse.jdt.ls.correction.extractLocal.assist"; //$NON-NLS-1$
	   const char* EXTRACT_LOCAL_NOT_REPLACE_ID = "org.eclipse.jdt.ls.correction.extractLocalNotReplaceOccurrences.assist"; //$NON-NLS-1$
	   const char* EXTRACT_CONSTANT_ID = "org.eclipse.jdt.ls.correction.extractConstant.assist"; //$NON-NLS-1$
	   const char* INLINE_LOCAL_ID = "org.eclipse.jdt.ls.correction.inlineLocal.assist"; //$NON-NLS-1$
	   const char* CONVERT_LOCAL_TO_FIELD_ID = "org.eclipse.jdt.ls.correction.convertLocalToField.assist"; //$NON-NLS-1$
	   const char* CONVERT_ANONYMOUS_TO_LOCAL_ID = "org.eclipse.jdt.ls.correction.convertAnonymousToLocal.assist"; //$NON-NLS-1$
	   const char* CONVERT_TO_STRING_BUFFER_ID = "org.eclipse.jdt.ls.correction.convertToStringBuffer.assist"; //$NON-NLS-1$
	   const char* CONVERT_TO_MESSAGE_FORMAT_ID = "org.eclipse.jdt.ls.correction.convertToMessageFormat.assist"; //$NON-NLS-1$;
	   const char* EXTRACT_METHOD_INPLACE_ID = "org.eclipse.jdt.ls.correction.extractMethodInplace.assist"; //$NON-NLS-1$;

	   const char* CONVERT_ANONYMOUS_CLASS_TO_NESTED_COMMAND = "convertAnonymousClassToNestedCommand";
  };

  void Reflect(Reader& reader, TypeHierarchyDirection& value) {
	  if (!reader.IsString())
	  {
		  value = TypeHierarchyDirection::Both;
		  return;
	  }
	  std::string v = reader.GetString();
	  if (v == "Children")
		  value = TypeHierarchyDirection::Both;
	  else if (v == "Parents")
		  value = TypeHierarchyDirection::Parents;
	  else if (v == "Both")
		  value = TypeHierarchyDirection::Both;
  }


  void Reflect(Writer& writer, TypeHierarchyDirection& value) {
	  switch (value)
	  {
	  case TypeHierarchyDirection::Children:
		  writer.String("Children");
		  break;
	  case TypeHierarchyDirection::Parents:
		  writer.String("Parents");
		  break;
	  case TypeHierarchyDirection::Both:
		  writer.String("Both");
		  break;
	  }
  }