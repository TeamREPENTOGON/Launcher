#include <cstdarg>
#include <future>

#include "launcher/repentogon_installer.h"
#include "launcher/version.h"
#include "shared/github_executor.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/sha256.h"
#include "shared/zip.h"

namespace Launcher {
	static const char* RepentogonReleasesURL = "https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases";
	/* GitHub URL of the latest release of Repentogon. */
	static const char* RepentogonURL = "https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases/latest";
	static const char* RepentogonZipName = "REPENTOGON.zip";
	static const char* HashName = "hash.txt";

	RepentogonInstallationState::RepentogonInstallationState() {

	}

	RepentogonInstaller::RepentogonInstaller(Installation* installation) : _installation(installation) {
		RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();
		if (repentogon.IsValid(true)) {
			_zhlVersion = repentogon.GetZHLVersion();
			_loaderVersion = repentogon.GetZHLLoaderVersion();
			_repentogonVersion = repentogon.GetRepentogonVersion();
		} else {
			_zhlVersion = _loaderVersion = _repentogonVersion = "none";
		}
	}

	bool RepentogonInstaller::InstallRepentogonThread(rapidjson::Document& response) {
		RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();
		if (!HandleLegacyInstallation()) {
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_DSOUND_REMOVAL_FAILED;
			return false;
		}

		PushNotification(false, "Downloading Repentogon release %s...", response["name"].GetString());

		const char* outputDir = _installation->GetIsaacInstallation().GetFolderPath().c_str();

		_installationState.Clear();
		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_CHECK_ASSETS;
		if (!CheckRepentogonAssets(response)) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: invalid assets\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_MISSING_ASSET;
			return false;
		}

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_DOWNLOAD;
		if (!DownloadRepentogon()) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: download failed\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_DOWNLOAD_ERROR;
			return false;
		}

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_CHECK_HASH;
		if (!CheckRepentogonIntegrity()) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: invalid zip\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_BAD_HASH;
			return false;
		}

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_UNPACK;
		if (!outputDir) {
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_INVALID_OUTPUT_DIR;
			return false;
		}

		if (!ExtractRepentogon(outputDir)) {
			Logger::Error("RepentogonUpdater::UpdateRepentogon: extraction failed\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_EXTRACT_FAILED;
			return false;
		}

		PushNotification(false, "Sucessfully installed Repentogon\n");

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_DONE;
		_installationState.result = REPENTOGON_INSTALLATION_RESULT_OK;

		return _installationState.result == REPENTOGON_INSTALLATION_RESULT_OK;
	}

	RepentogonMonitor<RepentogonInstaller::DownloadInstallRepentogonResult>
		RepentogonInstaller::InstallLatestRepentogon(bool force, bool allowPreReleases) {
		return std::make_tuple(std::async(std::launch::async, &RepentogonInstaller::InstallLatestRepentogonThread,
			this, force, allowPreReleases), &_monitor);
	}

	RepentogonMonitor<bool> RepentogonInstaller::InstallRepentogon(
		rapidjson::Document& release) {
		return std::make_tuple(std::async(std::launch::async, 
			&RepentogonInstaller::InstallRepentogonThread, this, std::ref(release)), 
			&_monitor);
	}

	RepentogonInstaller::DownloadInstallRepentogonResult
		RepentogonInstaller::InstallLatestRepentogonThread(bool force, bool allowPreReleases) {
		rapidjson::Document document;
		CheckRepentogonUpdatesResult checkUpdates = CheckRepentogonUpdatesThread(document, allowPreReleases, force);
		if (checkUpdates == CHECK_REPENTOGON_UPDATES_UTD) {
			return DOWNLOAD_INSTALL_REPENTOGON_UTD;
		}

		if (checkUpdates == CHECK_REPENTOGON_UPDATES_ERR) {
			return DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES;
		}

		return InstallRepentogonThread(document) ? DOWNLOAD_INSTALL_REPENTOGON_OK : DOWNLOAD_INSTALL_REPENTOGON_ERR;
	}

	RepentogonMonitor<RepentogonInstaller::CheckRepentogonUpdatesResult>
		RepentogonInstaller::CheckRepentogonUpdates(rapidjson::Document& document,
			bool allowPreReleases, bool force) {
		return std::make_tuple(std::async(std::launch::async, &RepentogonInstaller::CheckRepentogonUpdatesThread,
			this, std::ref(document), allowPreReleases, force), &_monitor);
	}

	RepentogonInstaller::CheckRepentogonUpdatesResult
		RepentogonInstaller::CheckRepentogonUpdatesThread(rapidjson::Document& document,
			bool allowPreReleases, bool force) {
		Github::VersionCheckResult result = CheckRepentogonUpdatesThread_Updater(document,
			allowPreReleases);

		if (result == Github::VERSION_CHECK_NEW)
			return CHECK_REPENTOGON_UPDATES_NEW;

		if (result == Github::VERSION_CHECK_UTD)
			return force ? CHECK_REPENTOGON_UPDATES_NEW : CHECK_REPENTOGON_UPDATES_UTD;

		return CHECK_REPENTOGON_UPDATES_ERR;
	}

	std::string RepentogonInstaller::GetDsoundDLLPath() const {
		IsaacInstallation const& isaac = _installation->GetIsaacInstallation();
		return isaac.GetFolderPath() + "\\dsound.dll";
	}

	bool RepentogonInstaller::NeedRemoveDsoundDLL() const {
		RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();

		if (repentogon.GetState() == REPENTOGON_INSTALLATION_STATUS_LEGACY) {
			return true;
		} else if (repentogon.GetState() == REPENTOGON_INSTALLATION_STATUS_NONE) {
			if (Filesystem::FileExists(GetDsoundDLLPath().c_str())) {
				return true;
			}
		}

		return false;
	}

	bool RepentogonInstaller::HandleLegacyInstallation() {
		bool needRemoveDsoundDLL = false;
		RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();

		if (NeedRemoveDsoundDLL()) {
			std::string dsoundPath = GetDsoundDLLPath();
			bool result = Filesystem::RemoveFile(dsoundPath.c_str());
			if (!result) {
				Logger::Error("Unable to delete dsound.dll: %d\n", GetLastError());
			} else {
				Logger::Info("Successfully deleted dsound.dll\n");
			}
			PushFileRemovalNotification(std::move(dsoundPath), result);
			return result;
		}

		return true;
	}

	void RepentogonInstaller::PushNotification(std::string&& text, bool error) {
		RepentogonInstallationNotification::GeneralNotification general;
		general._text = std::move(text);
		general._isError = error;

		RepentogonInstallationNotification notification(general);
		_monitor.Push(notification);
	}

	void RepentogonInstaller::PushFileRemovalNotification(std::string&& name, bool success) {
		RepentogonInstallationNotification::FileRemoval fileRemoval;
		fileRemoval._name = std::move(name);
		fileRemoval._ok = success;

		RepentogonInstallationNotification notification(fileRemoval);
		_monitor.Push(notification);
	}

	void RepentogonInstaller::PushNotification(bool isError, const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		int n = vsnprintf(NULL, 0, fmt, va);
		va_end(va);

		va_start(va, fmt);
		// God has forsaken us
		std::string s;
		try {
			s.resize(n + 1);
		} catch (std::exception&) {
			Logger::Error("Exception while resizing string in PushNotification");
			return;
		}
		vsnprintf(s.data(), n + 1, fmt, va);
		va_end(va);

		PushNotification(std::move(s), isError);
	}

	void RepentogonInstaller::PushFileDownloadNotification(std::string&& name, uint32_t size, uint32_t id) {
		RepentogonInstallationNotification::FileDownload fileDownload;
		fileDownload._filename = std::move(name);
		fileDownload._size = size;
		fileDownload._id = id;

		RepentogonInstallationNotification notification(fileDownload);
		_monitor.Push(notification);
	}

	bool RepentogonInstaller::CheckRepentogonAssets(rapidjson::Document const& data) {
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
				_installationState.hashOk = true;
				_installationState.hashUrl = url;
				hasHash = true;
			} else if (isRepentogon) {
				_installationState.zipOk = true;
				_installationState.zipUrl = url;
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

	bool RepentogonInstaller::DownloadRepentogon() {
		Github::DownloadMonitor monitor;
		std::future<Github::DownloadFileResult> hashDownload = sGithubExecutor->AddDownloadFileRequest(
			HashName, _installationState.hashUrl.c_str(), &monitor);
		Github::DownloadFileResult hashResult = GithubToRepInstall<Github::DownloadFileResult>(
			&monitor, hashDownload);

		if (hashResult != Github::DOWNLOAD_FILE_OK) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading hash\n");
			return false;
		}

		_installationState.hashFile = fopen(HashName, "r");
		if (!_installationState.hashFile) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: Unable to open %s for reading\n", HashName);
			return false;
		}

		std::future<Github::DownloadFileResult> zipDownload = sGithubExecutor->AddDownloadFileRequest(
			RepentogonZipName, _installationState.zipUrl.c_str(), &monitor);
		Github::DownloadFileResult zipResult = GithubToRepInstall<Github::DownloadFileResult>(
			&monitor, zipDownload);

		if (zipResult != Github::DOWNLOAD_FILE_OK) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading zip\n");
			return false;
		}

		_installationState.zipFile = fopen(RepentogonZipName, "r");
		if (!_installationState.zipFile) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: Unable to open %s for reading\n", RepentogonZipName);
			return false;
		}

		return true;
	}

	bool RepentogonInstaller::CheckRepentogonIntegrity() {
		if (!_installationState.hashFile || !_installationState.zipFile) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: hash or archive not previously opened\n");
			return false;
		}

		char hash[4096];
		char* result = fgets(hash, 4096, _installationState.hashFile);

		if (!result) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: fgets error\n");
			return false;
		}

		if (!feof(_installationState.hashFile)) {
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
		} catch (std::exception& e) {
			Logger::Fatal("RepentogonUpdater::CheckRepentogonIntegrity: exception while hashing %s: %s\n", RepentogonZipName, e.what());
			throw;
		}

		_installationState.hash = hash;
		_installationState.zipHash = zipHash;

		if (!Sha256::Equals(hash, zipHash.c_str())) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: hash mismatch: expected \"%s\", got \"%s\"\n", zipHash.c_str(), hash);
			return false;
		}

		return true;
	}

	bool RepentogonInstaller::ExtractRepentogon(const char* outputDir) {
		if (!outputDir) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: NULL output folder\n");
			return false;
		}

		if (!Filesystem::FolderExists(outputDir)) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: invalid output folder %s\n", outputDir);
			return false;
		}

		std::vector<std::tuple<std::string, bool>>& filesState = _installationState.unzipedFiles;
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

			std::string fullName = (outputDirBase + name);
			if (!Filesystem::CreateFileHierarchy(fullName.c_str(), "/")) {
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
				DWORD attributes = GetFileAttributesA(outputPath.c_str());
				bool fileOk = false;
				if (attributes != INVALID_FILE_ATTRIBUTES && attributes & FILE_ATTRIBUTE_DIRECTORY) {
					fileOk = true;
				} else {
					Logger::Error("RepentogonUpdater::ExtractRepentogon: cannot create file %s\n", name);
					ok = false;
				}

				zip_fclose(file);
				filesState.push_back(std::make_tuple(name, fileOk));
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
				PushNotification(false, "Extracting %s", name);
			}

			filesState.push_back(std::make_tuple(name, fileOk));
			fclose(output);
			zip_fclose(file);
		}

		zip_close(zip);
		return ok;
	}

	Github::VersionCheckResult RepentogonInstaller::CheckRepentogonUpdatesThread_Updater(rapidjson::Document& doc,
		bool allowPreReleases) {
		RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();

		Github::DownloadAsStringResult fetchResult = FetchRepentogonUpdates(doc, allowPreReleases);
		if (fetchResult != Github::DOWNLOAD_AS_STRING_OK) {
			return Github::VERSION_CHECK_ERROR;
		}

		if (repentogon.GetState() == REPENTOGON_INSTALLATION_STATUS_NONE) {
			return Github::VERSION_CHECK_NEW;
		}

		return Github::CheckUpdates(repentogon.GetZHLVersion().c_str(), "Repentogon", doc);
	}

	Github::DownloadAsStringResult RepentogonInstaller::FetchRepentogonUpdates(rapidjson::Document& response,
		bool allowPreReleases) {
		if (!allowPreReleases) {
			Github::DownloadMonitor monitor;
			std::future<Github::DownloadAsStringResult> result = sGithubExecutor->AddFetchReleasesRequest(
				RepentogonURL, response, &monitor);
			return GithubToRepInstall<Github::DownloadAsStringResult>(&monitor, result);
		} else {
			std::string allReleases;
			Github::DownloadAsStringResult allReleasesResult = Github::DownloadAsString(RepentogonReleasesURL, allReleases, nullptr);
			if (allReleasesResult != Github::DOWNLOAD_AS_STRING_OK) {
				Logger::Error("Unable to download list of all Repentogon releases: %d\n", allReleasesResult);
				Github::DownloadMonitor monitor;
				std::future<Github::DownloadAsStringResult> result = sGithubExecutor->AddFetchReleasesRequest(
					RepentogonURL, response, &monitor);
				return GithubToRepInstall<Github::DownloadAsStringResult>(&monitor, result);
			}

			rapidjson::Document asJson;
			asJson.Parse(allReleases.c_str());

			if (asJson.HasParseError()) {
				Logger::Error("Unable to parse list of all Repentogon releases as JSON\n");
				Github::DownloadMonitor monitor;
				std::future<Github::DownloadAsStringResult> result = sGithubExecutor->AddFetchReleasesRequest(
					RepentogonURL, response, &monitor);
				return GithubToRepInstall<Github::DownloadAsStringResult>(&monitor, result);
			}

			auto releases = asJson.GetArray();
			if (releases.Empty()) {
				Logger::Error("Trying to download a new Repentogon release, but none available\n");
				return Github::DOWNLOAD_AS_STRING_INVALID_JSON;
			}

			Github::DownloadMonitor monitor;
			std::future<Github::DownloadAsStringResult> result = sGithubExecutor->AddFetchReleasesRequest(
				releases[0]["url"].GetString(), response, &monitor);
			return GithubToRepInstall<Github::DownloadAsStringResult>(&monitor, result);
		}
	}

	void RepentogonInstallationState::Clear() {
		phase = REPENTOGON_INSTALLATION_PHASE_CHECK_ASSETS;
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