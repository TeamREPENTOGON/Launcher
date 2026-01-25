#include <filesystem>

#include "shared/zip.h"
#include "zip.h"
#include "shared/scoped_file.h"
#include "shared/logger.h"

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

	ExtractFileResult ExtractFile(zip_t*, int, zip_file_t* file, FILE* output) {
		char buffer[4096];
		zip_int64_t read = 0;
		while ((read = zip_fread(file, buffer, 4096)) > 0) {
			if (fwrite(buffer, (size_t)read, 1, output) != 1) {
				return EXTRACT_FILE_ERR_FWRITE;
			}
		}

		if (read == -1) {
			return EXTRACT_FILE_ERR_ZIP_FREAD;
		}

		return EXTRACT_FILE_OK;
	}

	bool ExtractAllToFolder(const char* filename, const char* outputDir) {
		Logger::Info("[Zip::ExtractAllToFolder] Extracting contents of `%s` to `%s`...\n", filename, outputDir);

		int error = 0;
		ScopedZip zip(zip_open(filename, ZIP_RDONLY | ZIP_CHECKCONS, &error));

		if (!zip) {
			Logger::Error("[Zip::ExtractAllToFolder] Failed to open zip file (%d)\n", error);
			return false;
		}

		const std::filesystem::path outputPath(outputDir);

		if (std::filesystem::exists(outputPath)) {
			if (!std::filesystem::is_directory(outputPath)) {
				Logger::Error("[Zip::ExtractAllToFolder] Output directory already exists, but it is not a directory!\n");
				return false;
			}
		} else {
			Logger::Info("[Zip::ExtractAllToFolder] Creating root directory: %s\n", outputDir);
			try {
				if (!std::filesystem::create_directories(outputPath)) {
					Logger::Error("[Zip::ExtractAllToFolder] No exception, but unable to create directory.");
					return false;
				}
			} catch (const std::filesystem::filesystem_error& e) {
				Logger::Error("[Zip::ExtractAllToFolder] Filesystem error attempting to create directory: %s", e.what());
				return false;
			}
		}

		bool extractError = false;

		const zip_int64_t nFiles = zip_get_num_entries(zip, 0);

		for (int i = 0; i < nFiles; ++i) {
			ScopedZipFile file(zip_fopen_index(zip, i, 0));
			if (!file) {
				Logger::Error("[Zip::ExtractAllToFolder] zip_fopen_index failed (%s)\n", zip_error_strerror(zip_get_error(zip)));
				return false;
			}

			const char* name = zip_get_name(zip, i, 0);
			if (!name) {
				Logger::Error("[Zip::ExtractAllToFolder] zip_get_name failed (%s)\n", zip_error_strerror(zip_get_error(zip)));
				return false;
			}
			const size_t nameLength = strlen(name);

			zip_stat_t fileStat;
			const int statResult = zip_stat_index(zip, i, 0, &fileStat);
			if (statResult) {
				Logger::Error("[Zip::ExtractAllToFolder] Failed to stat file %s (%d)\n", name, statResult);
				extractError = true;
			} else {
				const std::filesystem::path filePath = outputPath / name;
				const bool isDirectory = fileStat.size == 0 && (name[nameLength - 1] == '/' || name[nameLength - 1] == '\\');
				if (isDirectory || !filePath.has_filename()) {
					// Directories are not guarunteed to be detected separately within the zip, depending on how the zip was created.
					// Therefore we must create parent directories as needed for files.
					Logger::Info("[Zip::ExtractAllToFolder] Skipping directory %s\n", name);
					continue;
				}
				// Create parent directories as needed.
				if (filePath.has_parent_path() && !std::filesystem::exists(filePath.parent_path())) {
					Logger::Info("[Zip::ExtractAllToFolder] Creating sub-directory: %s\n", filePath.parent_path().string().c_str());
					try {
						if (!std::filesystem::create_directories(filePath.parent_path())) {
							Logger::Error("[Zip::ExtractAllToFolder] No exception, but unable to create directory.");
							return false;
						}
					} catch (const std::filesystem::filesystem_error& e) {
						Logger::Error("[Zip::ExtractAllToFolder] Filesystem error attempting to create directory: %s", e.what());
					}
				}
				Logger::Info("[Zip::ExtractAllToFolder] Extracting file %s\n", filePath.string().c_str());

				ScopedFile output(fopen(filePath.string().c_str(), "wb"));
				if (!output) {
					Logger::Error("[Zip::ExtractAllToFolder] fopen failed (%d)\n", (int)errno);
					return false;
				}

				const ExtractFileResult extractFileResult = ExtractFile(zip, i, file, output);
				if (extractFileResult != EXTRACT_FILE_OK) {
					Logger::Error("[Zip::ExtractAllToFolder] Failed to extract file (%d)\n", extractFileResult);
					switch (extractFileResult) {
					case Zip::EXTRACT_FILE_ERR_ZIP_FREAD:
						Logger::Error("[Zip::ExtractAllToFolder] Error while reading file content\n");
						break;

					case Zip::EXTRACT_FILE_ERR_FOPEN:
						Logger::Error("[Zip::ExtractAllToFolder] Unable to open file on disk to write content\n");
						break;

					case Zip::EXTRACT_FILE_ERR_FWRITE:
						Logger::Error("[Zip::ExtractAllToFolder] Unable to write extracted file\n");
						break;

					default:
						Logger::Error("[Zip::ExtractAllToFolder] Unexpected error %d\n", extractFileResult);
						break;
					}
					extractError = true;
				}
			}
		}

		return !extractError;
	}
}
