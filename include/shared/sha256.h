#pragma once

namespace Sha256 {
	/* Return the SHA-256 hash of the content of filename.
	 *
	 * If the function cannot load the content of the file in memory, or if
	 * the file does not exist, the function throws.
	 */
	std::string Sha256F(const char* filename) noexcept(false);

	/* Return the SHA-256 hash of the string. NULL strings have an empty
	 * hash. */
	std::string Sha256(const char* string, size_t size);

	/* Compare two hashes, performing case conversion if needed.
	 */
	bool Equals(const char* lhs, const char* rhs);

	/* Trim a hash string, removing trailing \n and \r, and leading whitespace
	 * and tabulations.
	 * If your "hash" contains other illegal characters, you should consider 
	 * fixing whatever generates it.
	 * 
	 * The char* version returns a pointer into the same string, offset to remove
	 * leading illegal characters, and with a \0 inserted to remove trailing
	 * illegal characters.
	 */
	char* Trim(char* hash);
	void Trim(std::string& hash);
}