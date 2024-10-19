#pragma once

#include <zip.h>

namespace Zip {
	enum ExtractFileResult {
		EXTRACT_FILE_OK,
		EXTRACT_FILE_ERR_FOPEN,
		EXTRACT_FILE_ERR_ZIP_FREAD
	};

	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file, const char* name);
}