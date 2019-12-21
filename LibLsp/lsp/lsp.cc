

#include "lru_cache.h"


#include <rapidjson/writer.h>


#include <stdio.h>
#include <iostream>
#include "location_type.h"
#include "out_list.h"
#include "lsTextDocumentIdentifier.h"
#include "lsVersionedTextDocumentIdentifier.h"
#include "lsResponseError.h"
#include "lsPosition.h"
#include "lsTextEdit.h"
#include "lsMarkedString.h"
#include "lsWorkspaceEdit.h"
#include "textDocument/code_action.h"
#include "textDocument/document_symbol.h"
#include "JavaExtentions/codeActionResult.h"
#include "textDocument/selectionRange.h"
#include "AbsolutePath.h"

#include <Windows.h>
#include "LibLsp/JsonRpc/json.h"
#include "language/language.h"
#include "network/uri/uri_builder.hpp"
#include "lsp_completion.h"

// namespace



lsTextDocumentIdentifier
lsVersionedTextDocumentIdentifier::AsTextDocumentIdentifier() const {
  lsTextDocumentIdentifier result;
  result.uri = uri;
  return result;
}


lsPosition::lsPosition() {}
lsPosition::lsPosition(int line, int character)
    : line(line), character(character) {}

bool lsPosition::operator==(const lsPosition& other) const {
  return line == other.line && character == other.character;
}

bool lsPosition::operator<(const lsPosition& other) const {
  return line != other.line ? line < other.line : character < other.character;
}

std::string lsPosition::ToString() const {
  return std::to_string(line) + ":" + std::to_string(character);
}
const lsPosition lsPosition::kZeroPosition = lsPosition();

lsRange::lsRange() {}
lsRange::lsRange(lsPosition start, lsPosition end) : start(start), end(end) {}

bool lsRange::operator==(const lsRange& o) const {
  return start == o.start && end == o.end;
}

bool lsRange::operator<(const lsRange& o) const {
  return !(start == o.start) ? start < o.start : end < o.end;
}

std::string lsRange::ToString() const
{
	std::stringstream ss;
	ss << "start:" << start.ToString() << std::endl;
	ss << "end" << end.ToString() << std::endl;
	return ss.str();
}

lsLocation::lsLocation() {}
lsLocation::lsLocation(lsDocumentUri uri, lsRange range)
    : uri(uri), range(range) {}

bool lsLocation::operator==(const lsLocation& o) const {
  return uri == o.uri && range == o.range;
}

bool lsLocation::operator<(const lsLocation& o) const {
  return std::make_tuple(uri.raw_uri_, range) <
         std::make_tuple(o.uri.raw_uri_, o.range);
}

bool lsTextEdit::operator==(const lsTextEdit& that) {
  return range == that.range && newText == that.newText;
}

std::string lsTextEdit::ToString() const
{
	std::stringstream ss;
	ss << "Range:" << range.ToString() << std::endl;
	ss << "newText:" << newText << std::endl;
	return ss.str();
}

void Reflect(Writer& visitor, lsMarkedString& value) {
  // If there is a language, emit a `{language:string, value:string}` object. If
  // not, emit a string.
  if (value.language) {
    REFLECT_MEMBER_START();
    REFLECT_MEMBER(language);
    REFLECT_MEMBER(value);
    REFLECT_MEMBER_END();
  } else {
    Reflect(visitor, value.value);
  }
}

void Reflect(Reader& visitor, lsMarkedString& value)
{
	REFLECT_MEMBER_START();
	REFLECT_MEMBER(language);
	REFLECT_MEMBER(value);
	REFLECT_MEMBER_END();
}

  void Reflect(Reader& visitor, LocationListEither::Either& value)
{
	  if(visitor.IsArray())
	  {
		  throw std::invalid_argument("Rsp_LocationListEither::Either& value is not array");
	  }
	  if (((JsonReader&)visitor).m_->GetArray()[0].HasMember("originSelectionRange"))
	  {
		  Reflect(visitor, value.second);
	  }
	  else
	  {
		  Reflect(visitor, value.first);
	  }
}

 void Reflect(Writer& visitor, LocationListEither::Either& value)
{
	if (value.first)
	{
		Reflect(visitor, value.first.value());
	}
	else if (value.second)
	{
		Reflect(visitor, value.second.value());
	}
}

lsWorkspaceEdit::~lsWorkspaceEdit()
{
	
}
void Reflect(Reader& visitor, TextDocumentCodeAction::Either& value)
{
	
	
	if(visitor.HasMember("command"))
	{
		if(visitor["command"]->IsString())
		{
			Reflect(visitor, value.first);
		}
		else
		{
			Reflect(visitor, value.second);
		}
	}
	else
	{
		if (visitor.HasMember("diagnostics") || visitor.HasMember("edit"))
		{
			Reflect(visitor, value.second);
		}
		else
		{
			Reflect(visitor, value.first);
		}
	}

}


void Reflect(Reader& visitor, lsWorkspaceEdit::Either& value)
{
	
	
	if(visitor.HasMember("textDocument"))
	{
		Reflect(visitor, value.first);
	}
	else
	{
		Reflect(visitor, value.second);
	}
}
ResourceOperation* GetResourceOperation(lsp::Any& lspAny)
{
	rapidjson::Document document;
	auto& data = lspAny.Data();
	document.Parse(data.c_str(), data.length());
	if (document.HasParseError()) {
		// ÌבÊ¾
		return nullptr;
	}
	auto find = document.FindMember("kind");

	JsonReader visitor{ &document };
	try
	{
		if (find->value == "create")
		{
			auto ptr = std::make_unique<lsCreateFile>();
			auto temp = ptr.get();
			Reflect(visitor, *temp);
			return ptr.release();
		}
		else if (find->value == "rename")
		{
			auto ptr = std::make_unique<lsRenameFile>();
			auto temp = ptr.get();
			Reflect(visitor, *temp);
			return ptr.release();
		}
		else if (find->value == "delete")
		{

			auto ptr = std::make_unique<lsDeleteFile>();
			auto temp = ptr.get();
			Reflect(visitor, *temp);
			return ptr.release();
		}
	}
	catch (std::exception&)
	{
		
	}
	return nullptr;
}

  void Reflect(Writer& visitor, ResourceOperation* value)
{
	
	if(!value)
	{
		throw std::invalid_argument("ResourceOperation value is nullptr");
	}
	if (value->kind == "create")
	{
		auto temp = (lsCreateFile*)value;
		Reflect(visitor, *temp);	
	}
	else if (value->kind == "rename")
	{
		auto temp = (lsRenameFile*)value;
		Reflect(visitor, *temp);
	}
	else if (value->kind == "delete")
	{

		auto temp = (lsDeleteFile*)value;
		Reflect(visitor, *temp);
	}
	
}

 void Reflect(Reader& visitor, lsp::Any& value)
{
	 
	 if (visitor.IsNull()) {
		 visitor.GetNull();
		 return;
	 }
	 JsonReader& json_reader = reinterpret_cast<JsonReader&>(visitor);
	 value.SetJsonString(visitor.ToString(), json_reader.m_->GetType());
}
 void Reflect(Writer& visitor, lsp::Any& value)
 {
	 JsonWriter& json_writer = reinterpret_cast<JsonWriter&>(visitor);
	 json_writer.m_->RawValue( value.Data().data(),value.Data().size(),static_cast<rapidjson::Type>(value.GetType()));
	
 }
lsCreateFile::lsCreateFile()
{
	kind = "create";
}

lsDeleteFile::lsDeleteFile()
{
	kind = "delete";
}

lsRenameFile::lsRenameFile()
{
	kind = "rename";
}


void Reflect(Reader& visitor, optional< SelectionRange* >& value)
{
	if (visitor.IsNull()) {
		visitor.GetNull();
		return;
	}

	SelectionRange* entry_value = nullptr;
	

		std::unique_ptr<SelectionRange> ptr = std::make_unique<SelectionRange>();
		SelectionRange* temp = ptr.get();
		Reflect(visitor, *temp);

		entry_value = ptr.release();
		value = (entry_value);

}
void Reflect(Writer& visitor, SelectionRange* value)
{

	if (!value)
	{
		throw std::invalid_argument("ResourceOperation value is nullptr");
	}
	
	Reflect(visitor, *value);
	

}

   std::string  make_file_scheme_uri(const std::string& absolute_path)
{
	   network::uri_builder builder;
	   builder.scheme("file");
	   builder.host("");
	   builder.path(absolute_path);
	   return  builder.uri().string();
	 ////  lsDocumentUri uri;
	 ////  uri.SetPath(absolute_path);
	 ///  return uri.raw_uri_;
}

// static
AbsolutePath AbsolutePath::BuildDoNotUse(const std::string& path) {
	AbsolutePath p;
	p.path = std::string(path);
	return p;
}


AbsolutePath::AbsolutePath() {}

bool IsUnixAbsolutePath(const std::string& path) {
	return !path.empty() && path[0] == '/';
}
bool IsWindowsAbsolutePath(const std::string& path) {
	auto is_drive_letter = [](char c) {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	};

	return path.size() > 3 && path[1] == ':' &&
		(path[2] == '/' || path[2] == '\\') && is_drive_letter(path[0]);
}

bool IsAbsolutePath(const std::string& path) {
	return IsUnixAbsolutePath(path) || IsWindowsAbsolutePath(path);
}



AbsolutePath::operator std::string() const {
	return path;
}

bool AbsolutePath::operator==(const AbsolutePath& rhs) const {
	return path == rhs.path;
}

bool AbsolutePath::operator!=(const AbsolutePath& rhs) const {
	return path != rhs.path;
}

void Reflect(Reader& visitor, AbsolutePath& value) {
	value.path = visitor.GetString();
}
void Reflect(Writer& visitor, AbsolutePath& value) {
	visitor.String(value.path.c_str(), value.path.length());
}

std::ostream& operator<<(std::ostream& out, const AbsolutePath& path) {
	out << path.path;
	return out;
}

lsDocumentUri lsDocumentUri::FromPath(const AbsolutePath& path) {
	lsDocumentUri result;
	result.SetPath(path);
	return result;
}
//void lsDocumentUri::SetPath(const AbsolutePath& path)
//{
//	raw_uri_ = make_file_scheme_uri(path.path);
//}
//
void lsDocumentUri::SetPath(const AbsolutePath& path) {
	// file:///c%3A/Users/jacob/Desktop/superindex/indexer/full_tests
	raw_uri_ = path;

	size_t index = raw_uri_.find(":");
	if (index == 1) {  // widows drive letters must always be 1 char
		raw_uri_.replace(raw_uri_.begin() + index, raw_uri_.begin() + index + 1,
			"%3A");
	}

	// subset of reserved characters from the URI standard
	// http://www.ecma-international.org/ecma-262/6.0/#sec-uri-syntax-and-semantics
	std::string t;
	t.reserve(8 + raw_uri_.size());

	// TODO: proper fix
#if defined(_WIN32)
	t += "file:///";
#else
	t += "file://";
#endif

	// clang-format off
	for (char c : raw_uri_)
		switch (c) {
		case ' ': t += "%20"; break;
		case '#': t += "%23"; break;
		case '$': t += "%24"; break;
		case '&': t += "%26"; break;
		case '(': t += "%28"; break;
		case ')': t += "%29"; break;
		case '+': t += "%2B"; break;
		case ',': t += "%2C"; break;
		case ';': t += "%3B"; break;
		case '?': t += "%3F"; break;
		case '@': t += "%40"; break;
		default: t += c; break;
		}
	// clang-format on
	raw_uri_ = std::move(t);
}

std::string lsDocumentUri::GetRawPath() const {


	if (raw_uri_.compare(0, 8, "file:///"))
		return raw_uri_;


	std::string ret;
#if defined(_WIN32)
	size_t i = 8;
#else
	size_t i = 7;
#endif
	auto from_hex = [](unsigned char c) {
		return c - '0' < 10 ? c - '0' : (c | 32) - 'a' + 10;
	};
	for (; i < raw_uri_.size(); i++) {
		if (i + 3 <= raw_uri_.size() && raw_uri_[i] == '%') {
			ret.push_back(from_hex(raw_uri_[i + 1]) * 16 + from_hex(raw_uri_[i + 2]));
			i += 2;
		}
		else
			ret.push_back(raw_uri_[i] == '\\' ? '/' : raw_uri_[i]);
	}
	return ret;
}

lsDocumentUri::lsDocumentUri() {}


lsDocumentUri::lsDocumentUri(const AbsolutePath& path)
{
	SetPath(path);
}

lsDocumentUri::lsDocumentUri(const lsDocumentUri& other): raw_uri_(other.raw_uri_)
{
}

bool lsDocumentUri::operator==(const lsDocumentUri& other) const {
	return raw_uri_ == other.raw_uri_;
}

bool lsDocumentUri::operator==(const std::string& other) const
{
	return raw_uri_ == other;
}

namespace
{
	std::wstring Utf8ToUnicode(const std::string& strUtf8)
	{
		int iLen = 0;
		wchar_t* pWchar = NULL;
		iLen = MultiByteToWideChar(CP_UTF8, 0, strUtf8.c_str(), -1, NULL, 0);
		pWchar = new wchar_t[iLen + 1];
		memset(pWchar, 0x00, (iLen + 1) * sizeof(wchar_t));
		MultiByteToWideChar(CP_UTF8, 0, strUtf8.c_str(), -1, pWchar, iLen);
		std::wstring wstrResult(pWchar);
		delete[] pWchar;
		
		return wstrResult;
	}

	std::string UnicodeToUtf8(const std::wstring& strUnicode)
	{
		int iLen = 0;
		char* pChar = NULL;
		iLen = WideCharToMultiByte(CP_UTF8, 0, strUnicode.c_str(), -1, NULL, 0, NULL, NULL);
		pChar = new char[iLen + 1];
		memset(pChar, 0x00, iLen + 1);
		WideCharToMultiByte(CP_UTF8, 0, strUnicode.c_str(), -1, pChar, iLen, NULL, NULL);
		std::string strResult(pChar);
		delete[] pChar;
		return strResult;
	}
}


AbsolutePath NormalizePath(const std::string& path0,
	bool ensure_exists = true,
	bool force_lower_on_windows = true) {
	// Requires Windows 8
	/*
	if (!PathCanonicalize(buffer, path.c_str()))
	  return nullopt;
	*/

	// Returns the volume name, ie, c:/
	/*
	if (!GetVolumePathName(path.c_str(), buffer, MAX_PATH))
	  return nullopt;
	*/
	std::wstring path= Utf8ToUnicode(path0);
	//std::string path = path0;

	wchar_t buffer[MAX_PATH] = (L"");

	// Normalize the path name, ie, resolve `..`.
	unsigned long len = GetFullPathName(path.c_str(), MAX_PATH, buffer, nullptr);
	if (!len)
		return {};
	path = std::wstring(buffer, len);

	// Get the actual casing of the path, ie, if the file on disk is `C:\FooBar`
	// and this function is called with `c:\fooBar` this will return `c:\FooBar`.
	// (drive casing is lowercase).
	if (ensure_exists) {
		len = GetLongPathName(path.c_str(), buffer, MAX_PATH);
		if (!len)
			return {};
		path = std::wstring(buffer, len);
	}

	// Empty paths have no meaning.
	if (path.empty())
		return {};

	// We may need to normalize the drive name to upper-case; at the moment
	// vscode sends lower-case path names.
	/*
	path[0] = toupper(path[0]);
	*/
	// Make the path all lower-case, since windows is case-insensitive.
	if (force_lower_on_windows) {
		for (size_t i = 0; i < path.size(); ++i)
			path[i] = (wchar_t)tolower(path[i]);
	}

	// cquery assumes forward-slashes.
	std::replace(path.begin(), path.end(), '\\', '/');

	
	return AbsolutePath(UnicodeToUtf8(path), false /*validate*/);
}
AbsolutePath lsDocumentUri::GetAbsolutePath() const {
	
	try
	{
		if (raw_uri_.find("file://") == 0){
			return NormalizePath(GetRawPath(), false /*ensure_exists*/, false);
		}
		else{
			return raw_uri_;
		}
	}
	catch (std::exception&)
	{
		return AbsolutePath("",false);
	}
}

AbsolutePath::AbsolutePath(const std::string& path, bool validate)
	: path(path) {
	// TODO: enable validation after fixing tests.
	if (validate && !IsAbsolutePath(path)) {
		qualify = false;
		auto temp = NormalizePath(path,false);
		if(temp.path.size())// try to fix
		{
			this->path = temp.path;
		}
	}
}

void Reflect(Writer& visitor, lsDocumentUri& value) {
	Reflect(visitor, value.raw_uri_);
}
void Reflect(Reader& visitor, lsDocumentUri& value) {
	Reflect(visitor, value.raw_uri_);
	// Only record the path when we deserialize a URI, since it most likely came
	// from the client.
	
}

 std::string ProgressReport::ToString() const
{
	std::string info;
	info += "id:" + id + "\n";
	info += "task:" + task + "\n";
	info += "subTask:" + subTask + "\n";
	info += "status:" + status + "\n";
	{
		std::stringstream ss;
		ss << "totalWork:" << totalWork << std::endl;
		info += ss.str();
	}
	{
		std::stringstream ss;
		ss << "workDone:" << workDone << std::endl;
		info += ss.str();
	}

	{
		std::stringstream ss;
		ss << "complete:" << complete << std::endl;
		info += ss.str();
	}

	return info;
}

std::string lsp::ToString(lsCompletionItemKind _kind)
{
	switch (_kind) {
	case lsCompletionItemKind::Text:
		return "Text";
	case lsCompletionItemKind::Method:
		return "Method";
	case lsCompletionItemKind::Function:
		return "";
	case lsCompletionItemKind::Constructor:
		return "Function";
	case lsCompletionItemKind::Field:
		return "Field";
	case lsCompletionItemKind::Variable:
		return "";
	case lsCompletionItemKind::Class:
		return "Variable";
	case lsCompletionItemKind::Interface:
		return "Interface";
	case lsCompletionItemKind::Module:
		return "Module";
	case lsCompletionItemKind::Property:
		return "Property";
	case lsCompletionItemKind::Unit:
		return "Unit";
	case lsCompletionItemKind::Value:
		return "Value";
	case lsCompletionItemKind::Enum:
		return "Enum";
	case lsCompletionItemKind::Keyword:
		return "Keyword";
	case lsCompletionItemKind::Snippet:
		return "Snippet";
	case lsCompletionItemKind::Color:
		return "Color";
	case lsCompletionItemKind::File:
		return "File";
	case lsCompletionItemKind::Reference:
		return "Reference";
	case lsCompletionItemKind::Folder:
		return "Folder";
	case lsCompletionItemKind::EnumMember:
		return "EnumMember";
	case lsCompletionItemKind::Constant:
		return "Constant";
	case lsCompletionItemKind::Struct:
		return "Struct";
	case lsCompletionItemKind::Event:
		return "Event";
	case lsCompletionItemKind::Operator:
		return "Operator";
	case lsCompletionItemKind::TypeParameter:
		return "TypeParameter";
	default:
		return "Unknown";
	}
}

std::string lsp::ToString(lsInsertTextFormat _kind)
{
	if (_kind == lsInsertTextFormat::PlainText)
	{
		return "PlainText";
	}
	else if (_kind == lsInsertTextFormat::Snippet)
	{
		return "Snippet";
	}else
	{
		return "Unknown";
	}
}

const std::string& lsCompletionItem::InsertedContent() const
{
	if (textEdit)
		return textEdit->newText;
	if (insertText.has_value() && !insertText->empty())
		return insertText.value();
	return label;
}

std::string lsCompletionItem::DisplayText()
{

	if (detail)
	{
		
		return label + " in " + detail.value();
	}
	return label;
}

std::string lsCompletionItem::ToString()
 {
	  std::stringstream info;
	  info << "label : " << label << std::endl;
	  if(kind)
		info << "kind : " << lsp::ToString(kind.value()) << std::endl;
	  else
		 info << "kind : no exist."  << std::endl;
	
	  if (detail)
		  info << "detail : " << detail.value() << std::endl;
	  else
		  info << "detail : no exist." << std::endl;

	  if (documentation)
	  {
		  info << "documentation : "  << std::endl;
	  	  if(documentation.value().first)
	  	  {
			  info << documentation.value().first.value();
	  	  }
		  else if(documentation.value().second)
		  {
			  info << documentation.value().second.value().value;
		  }
	  }
	  else
		  info << "documentation : no exist." << std::endl;

	  if (deprecated)
		  info << "deprecated : " << deprecated.value() << std::endl;
	  else
		  info << "deprecated : no exist." << std::endl;

	  if (preselect)
		  info << "preselect : " << preselect.value() << std::endl;
	  else
		  info << "preselect : no exist." << std::endl;

	  if (sortText)
		  info << "sortText : " << sortText.value() << std::endl;
	  else
		  info << "sortText : no exist." << std::endl;

	  if (filterText)
		  info << "filterText : " << filterText.value() << std::endl;
	  else
		  info << "filterText : no exist." << std::endl;


	  if (insertText)
		  info << "insertText : " << insertText.value() << std::endl;
	  else
		  info << "insertText : no exist." << std::endl;


	  if (insertTextFormat)
		  info << "insertText : " << lsp::ToString(insertTextFormat.value()) << std::endl;
	  else
		  info << "insertTextFormat : no exist." << std::endl;

	  if (textEdit)
		  info << "textEdit : " << textEdit.value().ToString() << std::endl;
	  else
		  info << "textEdit : no exist." << std::endl;


	
	  return  info.str();
	
 }
namespace  JDT
{
	namespace CodeActionKind {


		/**
		 * Base kind for quickfix actions: 'quickfix'
		 */
		  const char* QuickFix = "quickfix";

		/**
		 * Base kind for refactoring actions: 'refactor'
		 */
		const char* Refactor = "refactor";

		/**
		 * Base kind for refactoring extraction actions: 'refactor.extract'
		 *
		 * Example extract actions:
		 *
		 * - Extract method - Extract function - Extract variable - Extract interface
		 * from class - ...
		 */
		const char* RefactorExtract = "refactor.extract";

		/**
		 * Base kind for refactoring inline actions: 'refactor.inline'
		 *
		 * Example inline actions:
		 *
		 * - Inline function - Inline variable - Inline constant - ...
		 */
		const char* RefactorInline = "refactor.inline";

		/**
		 * Base kind for refactoring rewrite actions: 'refactor.rewrite'
		 *
		 * Example rewrite actions:
		 *
		 * - Convert JavaScript function to class - Add or remove parameter -
		 * Encapsulate field - Make method static - Move method to base class - ...
		 */
		const char* RefactorRewrite = "refactor.rewrite";

		/**
		 * Base kind for source actions: `source`
		 *
		 * Source code actions apply to the entire file.
		 */
		const char* Source = "source";

		/**
		 * Base kind for an organize imports source action: `source.organizeImports`
		 */
		const char* SourceOrganizeImports = "source.organizeImports";

		const char* COMMAND_ID_APPLY_EDIT = "java.apply.workspaceEdit";
		
	};


}