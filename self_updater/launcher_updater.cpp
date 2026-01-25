#include <iostream>
#include <sstream>

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
		data->_zipFilename = LauncherUpdateFileName;
		data->_hashUrl = _hashUrl;
		data->_zipUrl = _zipUrl;
	}
}