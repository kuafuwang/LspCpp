#pragma once

#include "LibLsp/JsonRpc/serializer.h"
#include <vector>
#include "lsDocumentUri.h"
#include "LibLsp/lsp/lsAny.h"
struct ResourceOperation {
	std::string kind;
	virtual  ~ResourceOperation() = default;
	
	MAKE_SWAP_METHOD(ResourceOperation, kind);
};
MAKE_REFLECT_STRUCT(ResourceOperation, kind);
extern void Reflect(Writer& visitor, ResourceOperation* value);
struct CreateFileOptions{
	
	/**
	 * Overwrite existing file. Overwrite wins over `ignoreIfExists`
	 */
	 bool overwrite = false;

	/**
	 * Ignore if exists.
	 */
	 bool ignoreIfExists =false;
	 void swap(CreateFileOptions& arg) noexcept
	 {
		 std::swap(overwrite, arg.overwrite);
		 std::swap(ignoreIfExists, arg.ignoreIfExists);
	 }
};
MAKE_REFLECT_STRUCT(CreateFileOptions, overwrite, ignoreIfExists);
struct lsCreateFile :public ResourceOperation {
	/**
	 * The resource to create.
	 */
	lsCreateFile();
	lsDocumentUri uri;

	/**
	 * Additional options
	 */
	CreateFileOptions options;

	MAKE_SWAP_METHOD(lsCreateFile, kind, uri, options);
};
MAKE_REFLECT_STRUCT(lsCreateFile, kind, uri,options);


struct DeleteFileOptions {
	/**
	 * Delete the content recursively if a folder is denoted.
	 */
	bool recursive = false;

	/**
	 * Ignore the operation if the file doesn't exist.
	 */
	bool ignoreIfNotExists = false;

	void swap(DeleteFileOptions& arg) noexcept
	{
		std::swap(recursive, arg.recursive);
		std::swap(ignoreIfNotExists, arg.ignoreIfNotExists);
	}
};

MAKE_REFLECT_STRUCT(DeleteFileOptions, recursive, ignoreIfNotExists);

struct lsDeleteFile :public ResourceOperation {
	/**
	 * The file to delete.
	 */
	lsDeleteFile();
	lsDocumentUri uri;

	/**
	 * Delete options.
	 */
	DeleteFileOptions options;

	MAKE_SWAP_METHOD(lsDeleteFile, kind, uri, options);
};
MAKE_REFLECT_STRUCT(lsDeleteFile, kind, uri,options);

typedef  CreateFileOptions RenameFileOptions;
struct lsRenameFile :public ResourceOperation {
	/**
	 * The old (existing) location.
	 */
	lsRenameFile();
	lsDocumentUri oldUri;

	/**
	 * The new location.
	 */

	lsDocumentUri newUri;

	/**
	 * Rename options.
	 */
	RenameFileOptions options;


	MAKE_SWAP_METHOD(lsRenameFile, kind, oldUri, newUri, options)
};
MAKE_REFLECT_STRUCT(lsRenameFile, kind, oldUri, newUri, options);


extern  ResourceOperation* GetResourceOperation(lsp::Any& lspAny);