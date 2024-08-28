#include <Windows.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "shared/filesystem.h"
#include "shared/sha256.h"

namespace Sha256 {
	std::string Sha256F(const char* filename) {
		WIN32_FIND_DATAA data;
		if (!Filesystem::FileExists(filename, &data)) {
			std::ostringstream s;
			s << "Updater::Sha256F: Attempt to hash non existant file " << filename;
			throw std::runtime_error(s.str());
		}

		std::unique_ptr<char[]> content(new char[data.nFileSizeLow]);
		if (!content) {
			std::ostringstream s;
			s << "Updater::Sha256F: Cannot hash " << filename << ": file size (" << data.nFileSizeLow << " bytes) would exceed available memory";
			throw std::runtime_error(s.str());
		}

		FILE* f = fopen(filename, "rb");
		fread(content.get(), data.nFileSizeLow, 1, f);

		return Sha256(content.get(), data.nFileSizeLow);
	}

	std::string Sha256(const char* str, size_t size) {
		BCRYPT_ALG_HANDLE alg;
		NTSTATUS err = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to open BCrypt SHA256 provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		DWORD buffSize;
		DWORD dummy;
		err = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (unsigned char*)&buffSize, sizeof(buffSize), &dummy, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to retrieve object length property of BCrypt SHA256 provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		BCRYPT_HASH_HANDLE hashHandle;
		std::unique_ptr<unsigned char[]> hashBuffer(new unsigned char[buffSize]);
		if (!hashBuffer) {
			throw std::runtime_error("Updater::Sha256: Unable to allocate buffer for internal computation");
		}

		err = BCryptCreateHash(alg, &hashHandle, hashBuffer.get(), buffSize, NULL, 0, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to create BCrypt hash object (" << err << ")";
			throw std::runtime_error(s.str());
		}

		err = BCryptHashData(hashHandle, (unsigned char*)str, size, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to hash data (" << err << ")";
			throw std::runtime_error(s.str());
		}

		DWORD hashSize;
		err = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (unsigned char*)&hashSize, sizeof(hashSize), &dummy, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to retrieve hash length property of BCrypt SHA256 provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		std::unique_ptr<unsigned char[]> hash(new unsigned char[hashSize]);
		if (!hash) {
			throw std::runtime_error("Updater::Sha256: Unable to allocate buffer for final computation");
		}

		err = BCryptFinishHash(hashHandle, hash.get(), hashSize, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to finish hashing (" << err << ")";
			throw std::runtime_error(s.str());
		}

		hashBuffer.reset();
		std::unique_ptr<char[]> hashHex(new char[hashSize * 2 + 1]);
		if (!hashHex) {
			throw std::runtime_error("Updater::Sha256: Unable to allocate buffer for hexdump of hash");
		}

		err = BCryptCloseAlgorithmProvider(alg, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Error while closing provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		for (int i = 0; i < hashSize; ++i) {
			sprintf(hashHex.get() + 2 * i, "%02hhx", hash[i]);
		}

		hashHex[hashSize * 2] = '\0';
		/* std::string will perform a copy of the content of the string,
		 * the unique_ptr can safely release it afterwards.
		 */

		std::string result(hashHex.get());
		return result;
	}

	bool Equals(const char* lhs, const char* rhs) {
		std::string left(lhs), right(rhs);
		std::transform(left.begin(), left.end(), left.begin(), std::toupper);
		std::transform(right.begin(), right.end(), right.begin(), std::toupper);
		return left == right;
	}
}