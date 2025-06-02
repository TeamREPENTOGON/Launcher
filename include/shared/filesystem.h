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
	bool Exists(const char* filename);

	std::string GetCurrentDirectory_();

	/* Remove a file. Return false on failure, true on success. */
	bool RemoveFile(const char* filename);

	bool SplitIntoComponents(const char* path, std::string* drive,
		std::string* filename, std::string* extension,
		std::vector<std::string>* folders);

	void TokenizePath(const char* path, std::vector<std::string>& tokens);
}