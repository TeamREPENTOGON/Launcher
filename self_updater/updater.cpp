#include <iostream>
#include <sstream>

#include "shared/logger.h"
#include "shared/sha256.h"
#include "shared/zip.h"
#include "self_updater/updater.h"

#include "zip.h"

namespace Updater {
	UpdaterBackend::UpdaterBackend(const char* lockFile, const char* from,
		const char* to, const char* url) {
		_lockFile = fopen(lockFile, "r");
		_fromVersion = from;
		_toVersion = to;
		_url = url;
		_lockFileName = lockFile;
		_state = UPDATE_STATE_INIT;
	}

	UpdateState UpdaterBackend::GetUpdateState() const {
		return _state;
	}

	UpdateStartupCheckResult UpdaterBackend::DoStartupCheck() {
		/* if (!_lockFile) {
			return UPDATE_STARTUP_CHECK_INVALID_FILE;
		}

		int result = fscanf(_lockFile, "%d", &_state);
		if (result != 1) {
			return UPDATE_STARTUP_CHECK_INVALID_CONTENT;
		} */

		if (_state != UPDATE_STATE_INIT) {
			return UPDATE_STARTUP_CHECK_INVALID_STATE;
		}

		_releaseDownloadResult = Github::FetchReleaseInfo(_url.c_str(), _releaseInfo, nullptr);
		if (_releaseDownloadResult != Github::DOWNLOAD_AS_STRING_OK) {
			return UPDATE_STARTUP_CHECK_CANNOT_FETCH_RELEASE;
		}

		if (!ValidateReleaseInfo()) {
			return UPDATE_STARTUP_CHECK_INVALID_RELEASE_INFO;
		}

		return UPDATE_STARTUP_CHECK_OK;
	}

	bool UpdaterBackend::CheckHashConsistency(const char* zipFile, const char* hash) {
		std::string fileHash = Sha256::Sha256F(zipFile);
		Logger::Info("UpdaterBackend::CheckHashConsistencty: fileHash = %s (%lu), hash = %s (%lu)\n", fileHash.c_str(), strlen(fileHash.c_str()), hash, strlen(hash));
		return Sha256::Equals(fileHash.c_str(), hash);
	}

	bool UpdaterBackend::DownloadUpdate(LauncherUpdateData* data) {
		PopulateUpdateData(data);

		data->_hashDownloadResult = Github::DownloadAsString(data->_hashUrl.c_str(), data->_hash, &data->_hashMonitor);
		data->_zipDownloadResult = Github::DownloadFile(data->_zipFileName.c_str(), data->_zipUrl.c_str(), &data->_zipMonitor);

		return data->_hashDownloadResult == Github::DOWNLOAD_AS_STRING_OK &&
			data->_zipDownloadResult == Github::DOWNLOAD_FILE_OK;
	}

	void UpdaterBackend::GenerateArchiveFilename(LauncherUpdateData* data) {
		char buffer[4096];
		sprintf(buffer, LauncherFileNameTemplate, _releaseInfo["name"].GetString());
		data->_zipFileName = buffer;
	}

	Github::DownloadAsStringResult UpdaterBackend::GetReleaseDownloadResult() const {
		return _releaseDownloadResult;
	}

	rapidjson::Document const& UpdaterBackend::GetReleaseInfo() const {
		return _releaseInfo;
	}

	ReleaseInfoState UpdaterBackend::GetReleaseInfoState() const {
		return _releaseInfoState;
	}

	bool UpdaterBackend::ValidateReleaseInfo() {
		auto assets = _releaseInfo["assets"].GetArray();
		bool foundZip = false, foundHash = false;

		for (int i = 0; i < assets.Size(); ++i) {
			auto const& value = assets[i];
			std::string name = value["name"].GetString();
			std::string url = value["browser_download_url"].GetString();

			if (name == "hash.txt") {
				foundHash = true;
				_hashUrl = url;
			}
			else if (name == "REPENTOGONLauncher.zip") {
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

	void UpdaterBackend::PopulateUpdateData(LauncherUpdateData* data) {
		GenerateArchiveFilename(data);
		data->_hashUrl = _hashUrl;
		data->_zipUrl = _zipUrl;
	}

	const std::string& UpdaterBackend::GetLockFileName() const {
		return _lockFileName;
	}

	ExtractArchiveResult UpdaterBackend::ExtractArchive(const char* name) {
		ExtractArchiveResult result;

		int error = 0;
		zip_t* zip = zip_open(name, ZIP_RDONLY | ZIP_CHECKCONS, &error);

		if (zip == NULL) {
			zip_close(zip);
			result.errCode = EXTRACT_ARCHIVE_ERR_CANNOT_OPEN;
			return result;
		}

		bool extractError = false;
		bool foundExe = false;

		int nFiles = zip_get_num_files(zip);
		for (int i = 0; i < nFiles; ++i) {
			zip_file_t* file = zip_fopen_index(zip, i, 0);
			if (!file) {
				result.SetZipError(zip_get_error(zip));
				zip_close(zip);
				return result;
			}

			const char* name = zip_get_name(zip, i, 0);
			if (!name) {
				result.SetZipError(zip_get_error(zip));
				zip_close(zip);
				return result;
			}

			if (!strcmp(name, "REPENTOGONLauncher.exe")) {
				foundExe = true;
			}

			Zip::ExtractFileResult extractFileResult = Zip::ExtractFile(zip, i, file, name);
			result.files.push_back(std::make_tuple(std::string(name), extractFileResult));

			if (!extractError) {
				extractError = extractFileResult != Zip::EXTRACT_FILE_OK;
			}
		}

		if (!foundExe) {
			result.errCode = EXTRACT_ARCHIVE_ERR_NO_EXE;
		}
		else if (extractError) {
			result.errCode = EXTRACT_ARCHIVE_ERR_FILE_EXTRACT;
		}
		else {
			result.errCode = EXTRACT_ARCHIVE_OK;
		}

		return result;
	}

	void ExtractArchiveResult::SetZipError(zip_error_t* error) {
		errCode = EXTRACT_ARCHIVE_ERR_ZIP_ERROR;
		zipError = zip_error_strerror(error);
	}
}