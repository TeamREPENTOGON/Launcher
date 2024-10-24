#include <WinSock2.h>
#include <Windows.h>
#include <bcrypt.h>
#include <Psapi.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <curl/curl.h>

#include "shared/curl/abstract_response_handler.h"
#include "shared/curl/file_response_handler.h"
#include "shared/curl/string_response_handler.h"
#include "shared/filesystem.h"
#include "shared/github.h"
#include "shared/logger.h"
#include "shared/scoped_curl.h"
#include "shared/sha256.h"

#include "launcher/filesystem.h"
#include "launcher/repentogon_updater.h"

#include "zip.h"

namespace Launcher {
	static const char* RepentogonReleasesURL = "https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases";
	/* GitHub URL of the latest release of Repentogon. */
	static const char* RepentogonURL = "https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases/latest";
	static const char* RepentogonZipName = "REPENTOGON.zip";
	static const char* HashName = "hash.txt";

	RepentogonUpdateState::RepentogonUpdateState() {

	}

	RepentogonUpdater::RepentogonUpdater() {

	}

	Github::VersionCheckResult RepentogonUpdater::CheckRepentogonUpdates(rapidjson::Document& doc,
		fs::Installation const& installation,
		bool allowPreReleases,
		Threading::Monitor<Github::GithubDownloadNotification>* monitor) {
		Github::DownloadAsStringResult fetchResult = FetchRepentogonUpdates(doc, allowPreReleases, monitor);
		if (fetchResult != Github::DOWNLOAD_AS_STRING_OK) {
			return Github::VERSION_CHECK_ERROR;
		}

		if (installation.GetRepentogonInstallationState() == fs::REPENTOGON_INSTALLATION_STATE_NONE) {
			return Github::VERSION_CHECK_NEW;
		}

		return Github::CheckUpdates(installation.GetZHLVersion().c_str(), "Repentogon", doc);
	}

	Github::DownloadAsStringResult RepentogonUpdater::FetchRepentogonUpdates(rapidjson::Document& response,
		bool allowPreReleases,
		Threading::Monitor<Github::GithubDownloadNotification>* monitor) {
		if (!allowPreReleases) {
			return Github::FetchReleaseInfo(RepentogonURL, response, monitor);
		} else {
			std::string allReleases;
			Github::DownloadAsStringResult allReleasesResult = Github::DownloadAsString(RepentogonReleasesURL, allReleases, nullptr);
			if (allReleasesResult != Github::DOWNLOAD_AS_STRING_OK) {
				Logger::Error("Unable to download list of all Repentogon releases: %d\n", allReleasesResult);
				return Github::FetchReleaseInfo(RepentogonURL, response, monitor);
			}

			rapidjson::Document asJson;
			asJson.Parse(allReleases.c_str());

			if (asJson.HasParseError()) {
				Logger::Error("Unable to parse list of all Repentogon releases as JSON\n");
				return Github::FetchReleaseInfo(RepentogonURL, response, monitor);
			}

			auto releases = asJson.GetArray();
			if (releases.Empty()) {
				Logger::Error("Trying to download a new Repentogon release, but none available\n");
				return Github::DOWNLOAD_AS_STRING_INVALID_JSON;
			}

			return Github::FetchReleaseInfo(releases[0]["url"].GetString(), response, monitor);
		}
	}

	RepentogonUpdateResult RepentogonUpdater::UpdateRepentogon(rapidjson::Document& data,
		const char* outputDir,
		Threading::Monitor<Github::GithubDownloadNotification>* monitor) {
		_repentogonUpdateState.Clear();

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_CHECK_ASSETS;
		if (!CheckRepentogonAssets(data)) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: invalid assets\n");
			return REPENTOGON_UPDATE_RESULT_MISSING_ASSET;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_DOWNLOAD;
		if (!DownloadRepentogon(monitor)) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: download failed\n");
			return REPENTOGON_UPDATE_RESULT_DOWNLOAD_ERROR;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_CHECK_HASH;
		if (!CheckRepentogonIntegrity()) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: invalid zip\n");
			return REPENTOGON_UPDATE_RESULT_BAD_HASH;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_UNPACK;
		if (!outputDir) {
			return REPENTOGON_UPDATE_RESULT_INVALID_OUTPUT_DIR;
		}

		if (!ExtractRepentogon(outputDir)) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: extraction failed\n");
			return REPENTOGON_UPDATE_RESULT_EXTRACT_FAILED;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_DONE;
		return REPENTOGON_UPDATE_RESULT_OK;
	}

	bool RepentogonUpdater::CheckRepentogonAssets(rapidjson::Document const& data) {
		if (!data.HasMember("assets")) {
			Logger::Error("RepentogonUpdater::CheckRepentogonAssets: no \"assets\" field\n");
			return false;
		}

		if (!data["assets"].IsArray()) {
			Logger::Error("RepentogonUpdater::CheckRepentogonAssets: \"assets\" field is not an array\n");
			return false;
		}

		rapidjson::GenericArray<true, rapidjson::Value> const& assets = data["assets"].GetArray();
		if (assets.Size() < 2) {
			Logger::Error("RepentogonUpdater::CheckRepentogonAssets: not enough assets\n");
			return false;
		}

		bool hasHash = false, hasZip = false;
		for (int i = 0; i < assets.Size() && (!hasHash || !hasZip); ++i) {
			if (!assets[i].IsObject())
				continue;

#define _GetObject GetObject
#undef GetObject
			rapidjson::GenericObject<true, rapidjson::Value> const& asset = assets[i].GetObject();
#define GetObject _GetObject
			if (!asset.HasMember("name") || !asset["name"].IsString())
				continue;

			const char* name = asset["name"].GetString();
			Logger::Info("RepentogonUpdater::CheckRepentogonAssets: asset %d -> %s\n", i, name);
			bool isHash = !strcmp(name, HashName);
			bool isRepentogon = !strcmp(name, RepentogonZipName);
			if (!isHash && !isRepentogon)
				continue;

			if (!asset.HasMember("browser_download_url") || !asset["browser_download_url"].IsString()) {
				Logger::Warn("RepentogonUpdater::CheckRepentogonAssets: asset %s has no /invalid \"browser_download_url\" field\n");
				continue;
			}

			const char* url = asset["browser_download_url"].GetString();
			if (isHash) {
				_repentogonUpdateState.hashOk = true;
				_repentogonUpdateState.hashUrl = url;
				hasHash = true;
			} else if (isRepentogon) {
				_repentogonUpdateState.zipOk = true;
				_repentogonUpdateState.zipUrl = url;
				hasZip = true;
			}
		}

		if (!hasHash) {
			Logger::Error("RepentogonUpdater::CheckRepentogonAssets: missing hash\n");
		}

		if (!hasZip) {
			Logger::Error("RepentogonUpdater::CheckRepentogonAssets: missing zip\n");
		}

		return hasHash && hasZip;
	}

	bool RepentogonUpdater::DownloadRepentogon(
		Threading::Monitor<Github::GithubDownloadNotification>* monitor) {
		if (Github::DownloadFile(HashName, _repentogonUpdateState.hashUrl.c_str(), monitor) != Github::DOWNLOAD_FILE_OK) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading hash\n");
			return false;
		}

		_repentogonUpdateState.hashFile = fopen(HashName, "r");
		if (!_repentogonUpdateState.hashFile) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: Unable to open %s for reading\n", HashName);
			return false;
		}

		if (Github::DownloadFile(RepentogonZipName, _repentogonUpdateState.zipUrl.c_str(), monitor) != Github::DOWNLOAD_FILE_OK) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading zip\n");
			return false;
		}

		_repentogonUpdateState.zipFile = fopen(RepentogonZipName, "r");
		if (!_repentogonUpdateState.zipFile) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: Unable to open %s for reading\n", RepentogonZipName);
			return false;
		}

		return true;
	}

	bool RepentogonUpdater::CheckRepentogonIntegrity() {
		if (!_repentogonUpdateState.hashFile || !_repentogonUpdateState.zipFile) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: hash or archive not previously opened\n");
			return false;
		}

		char hash[4096];
		char* result = fgets(hash, 4096, _repentogonUpdateState.hashFile);

		if (!result) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: fgets error\n");
			return false;
		}

		if (!feof(_repentogonUpdateState.hashFile)) {
			/* If EOF is not encountered, then there must be a newline character,
			 * otherwise the file is malformed.
			 */
			char* nl = strchr(hash, '\n');
			if (!nl) {
				Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: no newline found, malformed hash\n");
				return false;
			}

			*nl = '\0';
		}

		size_t len = strlen(hash);
		if (len != 64) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: hash is not 64 characters\n");
			return false;
		}

		std::string zipHash;
		try {
			zipHash = Sha256::Sha256F(RepentogonZipName);
		}
		catch (std::exception& e) {
			Logger::Fatal("RepentogonUpdater::CheckRepentogonIntegrity: exception while hashing %s: %s\n", RepentogonZipName, e.what());
			throw;
		}

		_repentogonUpdateState.hash = hash;
		_repentogonUpdateState.zipHash = zipHash;

		if (!Sha256::Equals(hash, zipHash.c_str())) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: hash mismatch: expected \"%s\", got \"%s\"\n", zipHash.c_str(), hash);
			return false;
		}

		return true;
	}

	bool RepentogonUpdater::ExtractRepentogon(const char* outputDir) {
		if (!outputDir) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: NULL output folder\n");
			return false;
		}
		
		if (!Filesystem::FolderExists(outputDir)) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: invalid output folder %s\n", outputDir);
			return false;
		}

		std::vector<std::tuple<std::string, bool>>& filesState = _repentogonUpdateState.unzipedFiles;
		int error;
		zip_t* zip = zip_open(RepentogonZipName, ZIP_RDONLY | ZIP_CHECKCONS, &error);
		if (!zip) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: error %d while opening %s\n", error, RepentogonZipName);
			return false;
		}

		bool ok = true;
		int nFiles = zip_get_num_entries(zip, 0);
		std::string outputDirBase(outputDir);
		for (int i = 0; i < nFiles; ++i) {
			zip_file_t* file = zip_fopen_index(zip, i, 0);
			if (!file) {
				Logger::Error("RepentogonUpdater::ExtractRepentogon: error opening file %d in archive\n", i);
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple("", false));
				continue;
			}

			const char* name = zip_get_name(zip, i, 0);
			if (!name) {
				zip_error_t* error = zip_get_error(zip);
				Logger::Error("RepentogonUpdater::ExtractRepentogon: error getting name of file %d in archive (%s)\n", i, zip_error_strerror(error));
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple("", false));
				continue;
			}

			if (!Filesystem::CreateFileHierarchy(name)) {
				Logger::Error("RepentogonUpdater::ExtractRepentogon: cannot create intermediate folders for file %s\n", name);
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple(name, false));
				continue;
			}

			std::string outputPath(outputDirBase);
			outputPath += "/";
			outputPath += name;
			FILE* output = fopen(outputPath.c_str(), "wb");
			if (!output) {
				Logger::Error("RepentogonUpdater::ExtractRepentogon: cannot create file %s\n", name);
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple(name, false));
				continue;
			}

			char buffer[4096];
			int count = zip_fread(file, buffer, 4096);
			bool fileOk = true;
			while (count != 0) {
				if (count == -1) {
					Logger::Error("RepentogonUpdater::ExtractRepentogon: error while reading %s\n", name);
					ok = false;
					fileOk = false;
					break;
				}

				if (fwrite(buffer, count, 1, output) != 1) {
					Logger::Error("RepentogonUpdater::ExtractRepentogon: error while writing %s\n", name);
					ok = false;
					fileOk = false;
					break;
				}

				count = zip_fread(file, buffer, 4096);
			}

			if (fileOk) {
				Logger::Info("RepentogonUpdater::ExtractRepentogon: successfully extracted %s to %s\n", name, outputPath.c_str());
			}

			filesState.push_back(std::make_tuple(name, fileOk));
			fclose(output);
			zip_fclose(file);
		}

		zip_close(zip);
		return ok;
	}

	RepentogonUpdateState const& RepentogonUpdater::GetRepentogonUpdateState() const {
		return _repentogonUpdateState;
	}

	void RepentogonUpdateState::Clear() {
		phase = REPENTOGON_UPDATE_PHASE_CHECK_ASSETS;
		zipOk = hashOk = false;
		hashUrl.clear();
		zipUrl.clear();
		hashFile = (FILE*)NULL;
		zipFile = (FILE*)NULL;
		hash.clear();
		zipHash.clear();
		unzipedFiles.clear();
	}
}