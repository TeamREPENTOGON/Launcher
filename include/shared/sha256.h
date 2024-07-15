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
}