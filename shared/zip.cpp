#include "shared/zip.h"
#include <zip.h>

namespace Zip {
	ExtractFileResult ExtractFile(zip_t* zip, int index, zip_file_t* file, const char* name) {
		FILE* f = fopen(name, "wb");
		if (!f) {
			return EXTRACT_FILE_ERR_FOPEN;
		}

		char buffer[4096];
		zip_int64_t read = 0;
		while ((read = zip_fread(file, buffer, 4096)) > 0) {
			fwrite(buffer, read, 1, f);
		}

		if (read == -1) {
			fclose(f);
			return EXTRACT_FILE_ERR_ZIP_FREAD;
		}

		fclose(f);
		return EXTRACT_FILE_OK;
	}
}