#include <iostream>
#include <sstream>

#include "comm/messages.h"
#include "shared/logger.h"
#include "shared/scoped_file.h"
#include "shared/sha256.h"
#include "shared/zip.h"
#include "self_updater/launcher_updater.h"

namespace Updater {
	LauncherUpdater::LauncherUpdater(const char* url) {
		_url = url;
		_state = UPDATE_STATE_INIT;
	}

	UpdateState LauncherUpdater::GetUpdateState() const {
		return _state;
	}

	UpdateStartupCheckResult LauncherUpdater::DoStartupCheck() {
		curl::RequestParameters request;
		Github::GenerateGithubHeaders(request);
		request.maxSpeed = request.timeout = request.serverTimeout = 0;
		request.url = _url;

		Github::FetchReleaseInfo(request, _releaseInfo, &_releaseDownloadResult, "launcher releases info");
		if (_releaseDownloadResult != curl::DOWNLOAD_STRING_OK) {
			return UPDATE_STARTUP_CHECK_CANNOT_FETCH_RELEASE;
		}

		if (!ValidateReleaseInfo()) {
			return UPDATE_STARTUP_CHECK_INVALID_RELEASE_INFO;
		}

		return UPDATE_STARTUP_CHECK_OK;
	}

	bool LauncherUpdater::CheckHashConsistency(const char* zipFile, const char* hash) {
		std::string fileHash;
		HashResult result = Sha256::Sha256F(zipFile, fileHash);

		if (result != HASH_OK) {
			Logger::Error("LauncherUpdater::CheckHashConsistency: error while computing hash "
				"of %s: %s\n", zipFile, HashResultToString(result));
			return false;
		}

		Logger::Info("LauncherUpdater::CheckHashConsistencty: fileHash = %s (%lu), hash = %s (%lu)\n",
			fileHash.c_str(), strlen(fileHash.c_str()), hash, strlen(hash));
		return Sha256::Equals(fileHash.c_str(), hash);
	}

	void LauncherUpdater::DownloadUpdate(LauncherUpdateData* data) {
		PopulateUpdateData(data);
		curl::RequestParameters request;
		request.maxSpeed = request.serverTimeout = request.timeout = 0;
		request.url = data->_hashUrl;
		Github::GenerateGithubHeaders(request);

		data->_hashDownloadDesc = curl::AsyncDownloadString(request, "update hash");
		request.url = data->_zipUrl;
		data->_zipDownloadDesc = curl::AsyncDownloadFile(request, data->_zipFilename);
	}

	void LauncherUpdater::GenerateArchiveFilename(LauncherUpdateData* data) {
		char buffer[4096];
		sprintf(buffer, LauncherFileNameTemplate, _releaseInfo["name"].GetString());
		data->_zipFilename = buffer;
	}

	curl::DownloadStringResult LauncherUpdater::GetReleaseDownloadResult() const {
		return _releaseDownloadResult;
	}

	rapidjson::Document const& LauncherUpdater::GetReleaseInfo() const {
		return _releaseInfo;
	}

	ReleaseInfoState LauncherUpdater::GetReleaseInfoState() const {
		return _releaseInfoState;
	}

	bool LauncherUpdater::ValidateReleaseInfo() {
		auto assets = _releaseInfo["assets"].GetArray();
		bool foundZip = false, foundHash = false;

		for (unsigned int i = 0; i < assets.Size(); ++i) {
			auto const& value = assets[i];
			std::string name = value["name"].GetString();
			std::string url = value["browser_download_url"].GetString();

			if (name == "hash.txt") {
				foundHash = true;
				_hashUrl = url;
			} else if (name == "REPENTOGONLauncher.zip") {
				foundZip = true;
				_zipUrl = url;
			}
		}

		if (!foundZip && !foundHash) {
			_releaseInfoState = RELEASE_INFO_STATE_NO_ASSETS;
			return false;
		}

		if (!foundZip) {
			_releaseInfoState = RELEASE_INFO_STATE_NO_ZIP;
			return false;
		}

		if (!foundHash) {
			_releaseInfoState = RELEASE_INFO_STATE_NO_HASH;
			return false;
		}

		_releaseInfoState = RELEASE_INFO_STATE_OK;
		return true;
	}

	void LauncherUpdater::PopulateUpdateData(LauncherUpdateData* data) {
		GenerateArchiveFilename(data);
		data->_hashUrl = _hashUrl;
		data->_zipUrl = _zipUrl;
	}

	ExtractArchiveResult LauncherUpdater::ExtractArchive(const char* filename) {
		class ScopedZip {
		public:
			ScopedZip(zip_t* zip) : _zip(zip) { }
			~ScopedZip() { if (_zip) zip_close(_zip); }

		private:
			zip_t* _zip = nullptr;
		};

		ExtractArchiveResult result;

		int error = 0;
		zip_t* zip = zip_open(filename, ZIP_RDONLY | ZIP_CHECKCONS, &error);
		ScopedZip scopedZip(zip);

		if (zip == NULL) {
			result.errCode = EXTRACT_ARCHIVE_ERR_CANNOT_OPEN_ZIP;
			return result;
		}

		const char* outputName = Comm::UnpackedArchiveName;
		FILE* output = fopen(outputName, "wb");
		ScopedFile scopedFile(output);
		if (!output) {
			result.errCode = EXTRACT_ARCHIVE_ERR_CANNOT_OPEN_BINARY_OUTPUT;
			return result;
		}

		bool extractError = false;
		bool foundExe = false;

		int nFiles = zip_get_num_files(zip);
		if (fwrite(&nFiles, sizeof(nFiles), 1, output) != 1) {
			result.errCode = EXTRACT_ARCHIVE_ERR_FWRITE;
			return result;
		}

		for (int i = 0; i < nFiles; ++i) {
			zip_file_t* file = zip_fopen_index(zip, i, 0);
			if (!file) {
				result.SetZipError(zip_get_error(zip));
				return result;
			}

			const char* name = zip_get_name(zip, i, 0);
			if (!name) {
				result.SetZipError(zip_get_error(zip));
				return result;
			}

			if (!strcmp(name, "REPENTOGONLauncher.exe")) {
				foundExe = true;
			}

			size_t nameLength = strlen(name);
			if (fwrite(&nameLength, sizeof(nameLength), 1, output) != 1) {
				result.errCode = EXTRACT_ARCHIVE_ERR_FWRITE;
				return result;
			}

			if (fwrite(name, nameLength, 1, output) != 1) {
				result.errCode = EXTRACT_ARCHIVE_ERR_FWRITE;
				return result;
			}

			zip_stat_t fileStat;
			int statResult = zip_stat_index(zip, i, 0, &fileStat);
			if (statResult) {
				result.files.push_back(std::make_tuple(std::string(name), Zip::EXTRACT_FILE_ERR_ZIP_STAT));
				extractError = true;
			} else {
				if (fwrite(&fileStat.size, sizeof(fileStat.size), 1, output) != 1) {
					result.errCode = EXTRACT_ARCHIVE_ERR_FWRITE;
					return result;
				}

				Zip::ExtractFileResult extractFileResult = Zip::ExtractFile(zip, i, file, output);
				result.files.push_back(std::make_tuple(std::string(name), extractFileResult));
				if (!extractError) {
					extractError = extractFileResult != Zip::EXTRACT_FILE_OK;
				}
			}
		}

		if (!foundExe) {
			result.errCode = EXTRACT_ARCHIVE_ERR_NO_EXE;
		} else if (extractError) {
			result.errCode = EXTRACT_ARCHIVE_ERR_FILE_EXTRACT;
		} else {
			result.errCode = EXTRACT_ARCHIVE_OK;
		}

		return result;
	}

	void ExtractArchiveResult::SetZipError(zip_error_t* error) {
		errCode = EXTRACT_ARCHIVE_ERR_ZIP_ERROR;
		zipError = zip_error_strerror(error);
	}
}