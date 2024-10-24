#include "shared/zip.h"
#include <zip.h>

namespace Zip {
	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file, const char* name, const char* mode) {
		FILE* f = fopen(name, mode);
		if (!f) {
			return EXTRACT_FILE_ERR_FOPEN;
		}

		ExtractFileResult result = ExtractFile(zip, index, file, f);
		fclose(f);
		return result;
	}

	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file, FILE* output) {
		char buffer[4096];
		zip_int64_t read = 0;
		while ((read = zip_fread(file, buffer, 4096)) > 0) {
			if (fwrite(buffer, read, 1, output) != 1) {
				return EXTRACT_FILE_ERR_FWRITE;
			}
		}

		if (read == -1) {
			return EXTRACT_FILE_ERR_ZIP_FREAD;
		}

		return EXTRACT_FILE_OK;
	}
}