#include <WinSock2.h>
#include <Windows.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "shared/filesystem.h"
#include "shared/sha256.h"

const char* HashResultToString(HashResult result) {
	switch (result) {
	case HASH_OK:
		return "OK";

	case HASH_INVALID_FILE:
		return "invalid file";

	case HASH_NO_MEMORY:
		return "no memory";

	case HASH_BCRYPT:
		return "bcrypt";

	default:
		std::terminate();
	}
}

namespace Sha256 {
	HashResult Sha256F(const char* filename, std::string& result) {
		WIN32_FIND_DATAA data;
		if (!Filesystem::FindFile(filename, &data)) {
			return HASH_INVALID_FILE;
		}

		std::unique_ptr<char[]> content(new char[data.nFileSizeLow]);
		if (!content) {
			return HASH_NO_MEMORY;
		}

		FILE* f = fopen(filename, "rb");
		fread(content.get(), data.nFileSizeLow, 1, f);
		fclose(f);

		return Sha256(content.get(), data.nFileSizeLow, result);
	}

	HashResult Sha256(const char* str, size_t size, std::string& result) {
		BCRYPT_ALG_HANDLE alg;
		NTSTATUS err = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
		if (!BCRYPT_SUCCESS(err)) {
			return HASH_BCRYPT;
		}

		DWORD buffSize;
		DWORD dummy;
		err = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (unsigned char*)&buffSize, sizeof(buffSize), &dummy, 0);
		if (!BCRYPT_SUCCESS(err)) {
			return HASH_BCRYPT;
		}

		BCRYPT_HASH_HANDLE hashHandle;
		std::unique_ptr<unsigned char[]> hashBuffer(new unsigned char[buffSize]);
		if (!hashBuffer) {
			return HASH_NO_MEMORY;
		}

		err = BCryptCreateHash(alg, &hashHandle, hashBuffer.get(), buffSize, NULL, 0, 0);
		if (!BCRYPT_SUCCESS(err)) {
			return HASH_BCRYPT;
		}

		err = BCryptHashData(hashHandle, (unsigned char*)str, size, 0);
		if (!BCRYPT_SUCCESS(err)) {
			return HASH_BCRYPT;
		}

		DWORD hashSize;
		err = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (unsigned char*)&hashSize, sizeof(hashSize), &dummy, 0);
		if (!BCRYPT_SUCCESS(err)) {
			return HASH_BCRYPT;
		}

		std::unique_ptr<unsigned char[]> hash(new unsigned char[hashSize]);
		if (!hash) {
			return HASH_NO_MEMORY;
		}

		err = BCryptFinishHash(hashHandle, hash.get(), hashSize, 0);
		if (!BCRYPT_SUCCESS(err)) {
			return HASH_BCRYPT;
		}

		hashBuffer.reset();
		std::unique_ptr<char[]> hashHex(new char[hashSize * 2 + 1]);
		if (!hashHex) {
			return HASH_NO_MEMORY;
		}

		err = BCryptCloseAlgorithmProvider(alg, 0);
		if (!BCRYPT_SUCCESS(err)) {
			return HASH_BCRYPT;
		}

		for (DWORD i = 0; i < hashSize; ++i) {
			sprintf(hashHex.get() + 2 * i, "%02hhx", hash[i]);
		}

		hashHex[hashSize * 2] = '\0';
		/* std::string will perform a copy of the content of the string,
		 * the unique_ptr can safely release it afterwards.
		 */

		result = hashHex.get();
		return HASH_OK;
	}

	bool Equals(const char* lhs, const char* rhs) {
		auto cv = [](std::string::value_type v) -> char {
			return (char)std::toupper(v);
		};

		std::string left(lhs), right(rhs);
		std::transform(left.begin(), left.end(), left.begin(), cv);
		std::transform(right.begin(), right.end(), right.begin(), cv);
		return left == right;
	}

	char* Trim(char* str) {
		size_t length = strlen(str);
		size_t i = 0;
		for (; i < length; ++i) {
			char value = str[i];
			if (!isspace(value) && value != '\t')
				break;
		}

		char* result = str + i;

		for (int j = length - 1; j >= i && j >= 0; --j) {
			char value = str[j];
			if (value != '\r' && value != '\n')
				break;
			str[j] = '\0';
		}

		return result;
	}

	void Trim(std::string& str) {
		int i = 0;
		int length = str.size();
		while (i < length && (isspace(str[i]) || str[i] == '\t'))
			++i;
		str.erase(0, i);

		length = str.size();
		int j = length - 1;
		while (j >= 0 && (str[j] == '\n' || str[j] == '\r'))
			--j;

		str.erase(j + 1, length - j - 1);
	}
}