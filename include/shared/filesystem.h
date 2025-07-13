#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include <string>
#include <vector>

namespace Filesystem {
	/* Create the hierarchy of folders required in order to create the file @name.
	 * sep is the string used as the separator inside name.
	 *
	 * Return true if folders are created successfully, false otherwise.
	 */
	bool CreateFileHierarchy(const char* name, const char* sep);

	/* Check if a folder with the given name exists. Return true on success,
	 * false on failure.
	 */
	bool IsFolder(const char* name);

	/* Check that a file with the given name exists. Fill the search
	 * structure with the result of the search.
	 *
	 * Return true on success, false on failure.
	 */
	bool FindFile(const char* filename, WIN32_FIND_DATAA* search);

	/* Check that a file with the given name exists. Return true on success,
	 * false on failure.
	 */
	bool Exists(const char* filename, HANDLE transaction = NULL);

	std::string GetCurrentDirectory_();

	/* Remove a file. Return false on failure, true on success.
	 *
	 * If the function fails, and a transaction was given, the transaction is
	 * not rolled back.
	 */
	bool RemoveFile(const char* filename, HANDLE transaction = NULL);

	/* Remove a folder, regardless of whether it is empty or not.
	 * If the handle is not null, the operation is transacted inside the given
	 * transaction.
	 *
	 * Return true on success, false if the function fails at any point. Failure
	 * at any point immediately stops the function; transacted operations are
	 * not rolled back.
	 */
	bool DeleteFolder(const char* path, HANDLE transaction = NULL);

	bool SplitIntoComponents(const char* path, std::string* drive,
		std::string* filename, std::string* extension,
		std::vector<std::string>* folders);

	void TokenizePath(const char* path, std::vector<std::string>& tokens);
}