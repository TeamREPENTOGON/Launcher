#include <iostream>
#include <sstream>

#include "shared/logger.h"
#include "shared/sha256.h"
#include "self_updater/updater.h"

namespace Updater {
	UpdaterBackend::UpdaterBackend(const char* lockFile, const char* from,
		const char* to, const char* url) {
		_lockFile = fopen(lockFile, "r");
		_fromVersion = from;
		_toVersion = to;
		_url = url;
		_lockFileName = lockFile;
	}

	UpdateState UpdaterBackend::GetUpdateState() const {
		return _state;
	}

	UpdateStartupCheckResult UpdaterBackend::DoStartupCheck() {
		if (!_lockFile) {
			return UPDATE_STARTUP_CHECK_INVALID_FILE;
		}

		int result = fscanf(_lockFile, "%d", &_state);
		if (result != 1) {
			return UPDATE_STARTUP_CHECK_INVALID_CONTENT;
		}

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
		std::string fileHash = Sha256::Sha256F(zipFile, true);
		Logger::Info("UpdaterBackend::CheckHashConsistencty: fileHash = %s (%lu), hash = %s (%lu)\n", fileHash.c_str(), strlen(fileHash.c_str()), hash, strlen(hash));
		return strcmp(fileHash.c_str(), hash) == 0;
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
}