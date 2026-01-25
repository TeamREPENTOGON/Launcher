#pragma once

#include <cstdio>
#include <zip.h>
#include <string>
#include <vector>
#include <tuple>

namespace Zip {
	class ScopedZip {
	  public:
		ScopedZip(zip_t* zip) : _zip(zip) {}
		~ScopedZip() { if (_zip) zip_close(_zip); }

		operator zip_t* () const { return _zip; }
		operator bool() const { return _zip != nullptr; }
	  private:
		zip_t* _zip = nullptr;
	};
	
	class ScopedZipFile {
	  public:
		ScopedZipFile(zip_file_t* zipFile) : _zipFile(zipFile) {}
		~ScopedZipFile() { if (_zipFile) zip_fclose(_zipFile); }

		operator zip_file_t* () const { return _zipFile; }
		operator bool() const { return _zipFile != nullptr; }
	  private:
		zip_file_t* _zipFile = nullptr;
	};

	enum ExtractFileResult {
		EXTRACT_FILE_OK,
		EXTRACT_FILE_ERR_FOPEN,
		EXTRACT_FILE_ERR_ZIP_FREAD,
		EXTRACT_FILE_ERR_FWRITE,
	};

	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file, 
		const char* name, const char* mode);
	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file,
		FILE* output);

	bool ExtractAllToFolder(const char* filename, const char* outputDir);
}
