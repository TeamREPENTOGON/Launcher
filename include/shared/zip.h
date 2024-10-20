#pragma once

#include <cstdio>
#include <zip.h>

namespace Zip {
	enum ExtractFileResult {
		EXTRACT_FILE_OK,
		EXTRACT_FILE_ERR_FOPEN,
		EXTRACT_FILE_ERR_ZIP_FREAD,
		EXTRACT_FILE_ERR_ZIP_STAT
	};

	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file, 
		const char* name, const char* mode);
	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file,
		FILE* output);
}