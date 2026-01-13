#include <cstdarg>
#include <future>
#include <regex>

#include "launcher/cli.h"
#include "launcher/repentogon_installer.h"
#include "launcher/standalone_rgon_folder.h"
#include "launcher/version.h"
#include "shared/github_executor.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/sha256.h"
#include "shared/zip.h"
#include "shared/gitlab_versionchecker.h"
#include "wx/msgdlg.h"

namespace fs = std::filesystem;

namespace Launcher {
	static const char* RepentogonReleasesURL = "https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases";
	/* GitHub URL of the latest release of Repentogon. */
	static const char* RepentogonURL = "https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases/latest";
	static const char* RepentogonZipName = "REPENTOGON.zip";
	static const char* HashName = "hash.txt";
	static const char* ReqLauncherVersionName = "launcher.txt";

	RepentogonInstallationState::RepentogonInstallationState() {

	}

	RepentogonInstaller::RepentogonInstaller(Installation* installation) : _installation(installation) {
		RepentogonInstallation const& repentogon = _installation->GetRepentogonInstallation();
		if (repentogon.IsValid()) {
			_zhlVersion = repentogon.GetZHLVersion();
			_loaderVersion = repentogon.GetZHLLoaderVersion();
			_repentogonVersion = repentogon.GetRepentogonVersion();
		} else {
			_zhlVersion = _loaderVersion = _repentogonVersion = "none";
		}
	}

	bool RepentogonInstaller::InstallRepentogonThread(rapidjson::Document& response) {
		if (!HandleLegacyInstallation()) {
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_DSOUND_REMOVAL_FAILED;
			return false;
		}

		PushNotification(false, "Downloading Repentogon release %s...", response["name"].GetString());

		const char* outputDir;
		fs::path path;
		InstallationData const& isaacData = _installation->GetIsaacInstallation().GetMainInstallation();
		if (!standalone_rgon::GenerateRepentogonPath(isaacData.GetFolderPath(),
			path, true
		)) { // _installation->GetIsaacInstallation().GetFolderPath().c_str();
			Logger::Fatal("RepentogonInstaller::InstallRepentogonThread: unable to generate "
				"Repentogon install folder name\n");
			std::terminate();
		}

		std::string s = path.string();
		outputDir = s.c_str();

		_installationState.Clear();
		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_CHECK_ASSETS;
		if (!outputDir) {
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_INVALID_OUTPUT_DIR;
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: no Repentogon output dir\n");
			return false;
		}

		if (!CheckRepentogonAssets(response)) {
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: invalid assets\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_MISSING_ASSET;
			if (!SteamUGC()) { //theres still the workshop for fallback now, so this is not yet a dealbreaker if its available, stillw ant to log the error tho
				return false;
			}
		}

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_DOWNLOAD;
		if (!DownloadRepentogon()) {
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: download failed\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_DOWNLOAD_ERROR;
			return false;
		}

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_CHECK_HASH;
		if (!CheckRepentogonIntegrity()) {
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: invalid zip\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_BAD_HASH;
			return false;
		}
		if (!CheckLauncherVersionRequirement()) {
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: doesnt meet the required launcher version\n");
			wxMessageBox(
				"REPENTOGON update failed to download due to an outdated launcher version.\n"
				"Please update it to continue to get the latest updates!\n",
				"Update your Launcher!",
				wxOK | wxOK_DEFAULT | wxICON_ERROR,
				NULL
			);
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_LAUNCHER_UPDATE_REQUIRED;
			return false;
		}

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_UNPACK;

		if (!CreateRepentogonFolder(outputDir)) {
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_NO_REPENTOGON;
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: unable to create Repentogon folder\n");
			return false;
		}

		if (!ExtractRepentogon(outputDir)) {
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: extraction failed\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_EXTRACT_FAILED;
			return false;
		}

		if (!CreateRepentogonMarker(outputDir)) {
			Logger::Error("RepentogonInstaller::InstallRepentogonThread: unable to create marker\n");
			_installationState.result = REPENTOGON_INSTALLATION_RESULT_NO_MARKER;
			return false;
		}
		if (!_installation->CheckRepentogonInstallation()) {
			if (isaacData.IsCompatibleWithRepentogon()) {
				PushNotification(false, "Copying Isaac files, please wait...");

				if (!standalone_rgon::CopyFiles(_installation->GetIsaacInstallation()
					.GetMainInstallation().GetFolderPath(),
					outputDir)) {
					Logger::Error("RepentogonInstaller::InstallRepentogonThread: unable to copy Isaac files\n");
					_installationState.result = REPENTOGON_INSTALLATION_RESULT_NO_ISAAC_COPY;
					return false;
				}

				if (!standalone_rgon::CreateFuckMethodFile(outputDir, standalone_rgon::REPENTOGON_FUCK_METHOD)) {
					Logger::Error("RepentogonInstaller::InstallRepentogonThread: unable to create Repentogon fuck method file\n");
					_installationState.result = REPENTOGON_INSTALLATION_RESULT_FUCK_METHOD;
				}

				if (!standalone_rgon::CreateSteamAppIDFile(outputDir)) {
					Logger::Error("RepentogonInstaller::InstallRepentogonThread: unable to create steam_appid.txt\n");
					_installationState.result = REPENTOGON_INSTALLATION_RESULT_NO_STEAM_APPID;
					return false;
				}

				if (isaacData.NeedsPatch()) {
					PushNotification(false, "Patching Isaac files, please wait...");
					if (!standalone_rgon::Patch(outputDir, (fs::current_path() / "patch").string())) {
						Logger::Error("RepentogonInstaller::InstallRepentogonThread: unable to patch Isaac files\n");
						_installationState.result = REPENTOGON_INSTALLATION_RESULT_NO_ISAAC_PATCH;
						return false;

					}
				}
			}
			else {
				Logger::Warn("RepentogonInstaller::InstallRepentogonThread: Skipped copy/patch routine since the vanilla installation is not compatible\n");
			}
		}
		else {
			Logger::Warn("RepentogonInstaller::InstallRepentogonThread: Skipped copy/patch routine since there's a pre-existent valid instalation.\n");
		}

		PushNotification(false, "Sucessfully installed Repentogon\n");


		Logger::Info("RepentogonInstaller::InstallRepentogonThread: successfully installed Repentogon\n");

		_installationState.phase = REPENTOGON_INSTALLATION_PHASE_DONE;
		_installationState.result = REPENTOGON_INSTALLATION_RESULT_OK;

		return _installationState.result == REPENTOGON_INSTALLATION_RESULT_OK;
	}

	bool RepentogonInstaller::CreateRepentogonFolder(const char* outputDir) {
		DWORD result = CreateDirectoryA(outputDir, NULL);
		return result || GetLastError() == ERROR_ALREADY_EXISTS;
	}

	bool RepentogonInstaller::CreateRepentogonMarker(const char* outputDir) {
		if (!outputDir) {
			Logger::Error("RepentogonInstaller::CreateRepentogonMarker: null output dir\n");
			return false;
		}

		fs::path path(outputDir);
		path /= RepentogonInstallation::RepentogonMarker;

		HANDLE result = CreateFileA(path.string().c_str(), GENERIC_READ, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
		if (result == INVALID_HANDLE_VALUE) {
			Logger::Error("RepentogonInstaller::CreateRepentogonMarker: unable to create marker (%d)\n", GetLastError());
			return false;
		}

		CloseHandle(result);
		return true;
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

		if (checkUpdates == CHECK_REPENTOGON_UPDATES_GITLAB_SKIP) {
			return DOWNLOAD_INSTALL_REPENTOGON_OK;
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
		//Gitlab Filter
		if (!force) {
			std::string versionfilename = "version";
			if (allowPreReleases) {
				versionfilename = "versionnightly";
			}
			if (RemoteGitLabVersionMatches(versionfilename, _installation->GetRepentogonInstallation().GetRepentogonVersion())) {
				return CHECK_REPENTOGON_UPDATES_GITLAB_SKIP;
			}
		}
		//Gitlab Filter END
		Github::VersionCheckResult result = CheckRepentogonUpdatesThread_Updater(document,
			allowPreReleases);



		if (result == Github::VERSION_CHECK_NEW)
			return CHECK_REPENTOGON_UPDATES_NEW;

		if (result == Github::VERSION_CHECK_UTD)
			return force ? CHECK_REPENTOGON_UPDATES_NEW : CHECK_REPENTOGON_UPDATES_UTD;

		return CHECK_REPENTOGON_UPDATES_ERR;
	}

	std::vector<std::tuple<bool, std::string>> RepentogonInstaller::GetDsoundDLLState() const {
		IsaacInstallation const& installation = _installation->GetIsaacInstallation();
		std::vector<std::string> paths;
		paths.push_back(installation.GetMainInstallation().GetFolderPath() + "\\dsound.dll");
		paths.push_back(installation.GetRepentogonInstallation().GetFolderPath() + "\\dsound.dll");

		std::vector<std::tuple<bool, std::string>> result;
		for (std::string& path : paths) {
			bool exists = Filesystem::Exists(path.c_str());
			result.push_back(std::make_tuple(exists, std::move(path)));
		}

		return result;
	}

	bool RepentogonInstaller::HandleLegacyInstallation() {
		std::vector<std::tuple<bool, std::string>> dsounds = GetDsoundDLLState();
		for (auto& [exists, dsoundPath] : dsounds) {
			if (exists) {
				/* Note that there is a filesystem race between the check above
				 * and the deletion below.
				 */
				bool result = Filesystem::RemoveFile(dsoundPath.c_str());
				if (!result && GetLastError() != ERROR_FILE_NOT_FOUND) {
					Logger::Error("Unable to delete %s: %d\n", dsoundPath.c_str(), GetLastError());
				} else {
					Logger::Info("Successfully deleted %s\n", dsoundPath.c_str());
				}
				PushFileRemovalNotification(std::move(dsoundPath), result);
				return result;
			}
		}

		return true;
	}

	void RepentogonInstaller::PushNotification(std::string text, bool error) {
		RepentogonInstallationNotification::GeneralNotification general;
		general._text = std::move(text);
		general._isError = error;

		RepentogonInstallationNotification notification(general);
		_monitor.Push(notification);
	}

	void RepentogonInstaller::PushFileRemovalNotification(std::string name, bool success) {
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

	void RepentogonInstaller::PushFileDownloadNotification(std::string name, uint32_t size, uint32_t id) {
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
		for (unsigned int i = 0; i < assets.Size() && (!hasHash || !hasZip); ++i) {
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
			bool isLauncherReq = !strcmp(name, ReqLauncherVersionName);
			bool isRepentogon = !strcmp(name, RepentogonZipName);
			if (!isHash && !isRepentogon && !isLauncherReq)
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
			}else if (isLauncherReq) {
				_installationState.launcherversionlock = true;
				_installationState.launcherversionreqUrl = url;
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


	bool WaitForWorkshopDownload(PublishedFileId_t fileId, uint32 timeoutMs = 60000)
	{
		uint32 elapsed = 0;
		const uint32 step = 100;

		while (elapsed < timeoutMs)
		{
			SteamAPI_RunCallbacks();

			uint32 state = SteamUGC()->GetItemState(fileId);

			if (state & k_EItemStateInstalled)
				return true;

			if (state & k_EItemStateDownloadPending ||
				state & k_EItemStateDownloading)
			{
				Sleep(step);
				elapsed += step;
				continue;
			}
			SteamUGC()->DownloadItem(fileId, true);
			Sleep(step);
			elapsed += step;
		}

		return false;
	}

	bool SubscribeDownloadAndGetFile(
		PublishedFileId_t workshopId,
		const std::string& relativeFilePath,
		std::string& outFullPath)
	{
		if (!SteamUGC())
			return false;
		SteamUGC()->SubscribeItem(workshopId);
		if (!WaitForWorkshopDownload(workshopId))
			return false;

		uint64 sizeOnDisk = 0;
		char installPath[1024] = {};
		uint32 timeStamp = 0;

		if (!SteamUGC()->GetItemInstallInfo(
			workshopId,
			&sizeOnDisk,
			installPath,
			sizeof(installPath),
			&timeStamp))
			return false;

		outFullPath = std::string(installPath) + "/" + relativeFilePath;

		if (GetFileAttributesA(outFullPath.c_str()) == INVALID_FILE_ATTRIBUTES)
			return false;

		return true;
	}


	bool RepentogonInstaller::DownloadRepentogon() {
		// curl::DownloadMonitor monitor;
		curl::RequestParameters request;
		request.maxSpeed = sCLI->CurlLimit();
		request.timeout = sCLI->CurlTimeout();
		request.serverTimeout = sCLI->CurlConnectTimeout();
		request.url = _installationState.hashUrl;

		Github::GenerateGithubHeaders(request);

		std::shared_ptr<curl::AsynchronousDownloadFileDescriptor> hashDownloadDescriptor =
			curl::AsyncDownloadFile(request, HashName);
		std::optional<curl::DownloadFileDescriptor> hashResult = GithubToRepInstall<curl::DownloadFileDescriptor>(
			&hashDownloadDescriptor->base.monitor, hashDownloadDescriptor->result);

		if (!hashResult) {
			hashDownloadDescriptor->base.cancel.store(true, std::memory_order_release);
			Logger::Info("RepentogonUpdater::DownloadRepentogon: cancel requested\n");
			return false;
		}

		if (hashResult->result != curl::DOWNLOAD_FILE_OK) {
			std::string filePath;
			std::string relpath = HashName;
			Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading hash, trying steam\n");
			if (SubscribeDownloadAndGetFile(3643104060, "REPENTOGON/" + relpath, filePath)) { //3643104060 is dummy id for testing
				_installationState.hashFile = fopen(filePath.c_str(), "r");
			}
			else {
				Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading hash from steam\n");
				return false;
			}
		}
		else {
			_installationState.hashFile = fopen(HashName, "r");
		}

		if (!_installationState.hashFile) {
			Logger::Error("RepentogonUpdater::DownloadRepentogon: Unable to open %s for reading\n", HashName);
			return false;
		}

		if (_installationState.launcherversionlock) {
			request.url = _installationState.launcherversionreqUrl;

			Github::GenerateGithubHeaders(request);

			std::shared_ptr<curl::AsynchronousDownloadFileDescriptor> ReqLauncherverDownloadDescriptor =
				curl::AsyncDownloadFile(request, ReqLauncherVersionName);
			std::optional<curl::DownloadFileDescriptor> reqlauncherResult = GithubToRepInstall<curl::DownloadFileDescriptor>(
				&ReqLauncherverDownloadDescriptor->base.monitor, ReqLauncherverDownloadDescriptor->result);

			if (!reqlauncherResult) {
				ReqLauncherverDownloadDescriptor->base.cancel.store(true, std::memory_order_release);
				Logger::Info("RepentogonUpdater::DownloadRepentogon: cancel requested\n");
				return false;
			}



			if (reqlauncherResult->result != curl::DOWNLOAD_FILE_OK) {
				std::string filePath;
				std::string relpath = ReqLauncherVersionName;
				Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading launcher version.txt trying steam\n");
				if (SubscribeDownloadAndGetFile(3643104060, "REPENTOGON/" + relpath, filePath)) { //3643104060 is dummy id for testing
					_installationState.ReqVersionFile = fopen(filePath.c_str(), "r");
				}
				else {
					Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading launcher version.txt from steam\n");
					return false;
				}
			}
			else {
				_installationState.ReqVersionFile = fopen(ReqLauncherVersionName, "r");
			}
			
			if (!_installationState.ReqVersionFile) {
				Logger::Error("RepentogonUpdater::DownloadRepentogon: Unable to open %s for reading\n", ReqLauncherVersionName);
				return false;
			}
		}
		request.url = _installationState.zipUrl;
		std::shared_ptr<curl::AsynchronousDownloadFileDescriptor> zipDownloadDesc =
			curl::AsyncDownloadFile(request, RepentogonZipName);
		std::optional<curl::DownloadFileDescriptor> zipResult = GithubToRepInstall<curl::DownloadFileDescriptor>(
			&zipDownloadDesc->base.monitor, zipDownloadDesc->result);

		if (!zipResult) {
			zipDownloadDesc->base.cancel.store(true, std::memory_order_release);
			Logger::Info("RepentogonUpdater::DownloadRepentogon: cancel requested\n");
			return false;
		}

		if (zipResult->result != curl::DOWNLOAD_FILE_OK) {
			std::string filePath;
			std::string relpath = RepentogonZipName;
			Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading zip, trying steam\n");
			if (SubscribeDownloadAndGetFile(3643104060, "REPENTOGON/" + relpath, filePath)) { //3643104060 is dummy id for testing
				_installationState.zipFile = fopen(filePath.c_str(), "r");
				fs::copy_file(filePath,fs::current_path() / RepentogonZipName,fs::copy_options::overwrite_existing); //needed for hashing later
			}
			else {
				Logger::Error("RepentogonUpdater::DownloadRepentogon: error while downloading zip from steam\n");
				return false;
			}
		}
		else {
			_installationState.zipFile = fopen(RepentogonZipName, "r");
		}

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
		HashResult hashResult = Sha256::Sha256F(RepentogonZipName, zipHash);

		if (hashResult != HASH_OK) {
			Logger::Fatal("RepentogonUpdater::CheckRepentogonIntegrity: unable "
				"to compute hash of %s: %s\n", RepentogonZipName, HashResultToString(hashResult));
		}

		_installationState.hash = hash;
		_installationState.zipHash = zipHash;

		if (!Sha256::Equals(hash, zipHash.c_str())) {
			Logger::Error("RepentogonUpdater::CheckRepentogonIntegrity: hash mismatch: expected \"%s\", got \"%s\"\n", zipHash.c_str(), hash);
			return false;
		}

		return true;
	}
	
	struct Version {
		std::vector<int> numbers;
		bool isBeta = false;
		bool isUnstable = false;
		bool isDev = false;
	};

	Version parseVersion(const std::string& s) {
		Version v;

		if (s.empty())
			return v;

		// Make a copy so we can manipulate it
		std::string str = s;

		// Normalize to lowercase
		std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });

		if (str == "dev") {
			v.isDev = true;
			return v;
		}

		// Trim leading v
		if (str[0] == 'v')
			str.erase(0, 1);

		// Split into tokens
		std::regex re(R"([.\-_]+)");
		std::sregex_token_iterator first{str.begin(), str.end(), re, -1}, last;
		std::vector<std::string> tokens{first, last};

		for (const std::string& token : tokens) {
			if (token == "beta") {
				v.isBeta = true;
			} else if (token == "unstable") {
				v.isUnstable = true;
			} else {
				try {
					v.numbers.push_back(std::stoi(token));
				} catch (...) {
					Logger::Error("RepentogonUpdater::parseVersion: Unrecognized token `%s` found in version string `%s`\n", token.c_str(), s.c_str());
				}
			}
		}

		return v;
	}

	int compareVersions(const std::string& a, const std::string& b) {
		Version va = parseVersion(a);
		Version vb = parseVersion(b);

		// "dev" > everything else
		if (va.isDev || vb.isDev) {
			if (va.isDev && vb.isDev) {
				return 0;
			}
			return va.isDev ? 1 : -1;
		}

		size_t maxParts = std::max(va.numbers.size(), vb.numbers.size());
		for (size_t i = 0; i < maxParts; ++i) {
			int na = (i < va.numbers.size()) ? va.numbers[i] : 0;
			int nb = (i < vb.numbers.size()) ? vb.numbers[i] : 0;

			if (na < nb) return -1;
			if (na > nb) return 1;
		}

		//same numbers then release > beta > unstable
		if (va.isUnstable != vb.isUnstable) {
			return va.isUnstable ? -1 : 1;
		}
		if (va.isBeta != vb.isBeta) {
			return va.isBeta ? -1 : 1;
		}

		return 0;
	}

	bool RepentogonInstaller::CheckLauncherVersionRequirement() {
		if (!_installationState.launcherversionlock) {
			return true;
		}
		char reqverbuffer[4096];
		char* result = fgets(reqverbuffer, 4096, _installationState.ReqVersionFile);

		if (!result) {
			Logger::Error("RepentogonUpdater::CheckLauncherVersionRequirement: fgets error\n");
			return false;
		}
		std::string reqver = reqverbuffer;
		std::string currver = CMAKE_LAUNCHER_VERSION;

		return compareVersions(currver,reqver) >= 0;
	}

	bool RepentogonInstaller::ExtractRepentogon(const char* outputDir) {
		if (!outputDir) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: NULL output folder\n");
			return false;
		}

		if (!Filesystem::IsFolder(outputDir)) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: invalid output folder %s\n", outputDir);
			return false;
		}

		std::vector<std::tuple<std::string, bool>>& filesState = _installationState.unzipedFiles;
		int zipError;
		zip_t* zip = zip_open(RepentogonZipName, ZIP_RDONLY | ZIP_CHECKCONS, &zipError);
		if (!zip) {
			Logger::Error("RepentogonUpdater::ExtractRepentogon: error %d while opening %s\n", zipError, RepentogonZipName);
			return false;
		}

		bool ok = true;
		int nFiles = zip_get_num_entries(zip, 0);
		std::string outputDirBase(outputDir);
		outputDirBase += "/";
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
				Logger::Error("RepentogonUpdater::ExtractRepentogon: error getting name of file %d in archive (%s)\n",
					i, zip_error_strerror(error));
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

		Github::ReleaseInfoResult fetchResult = FetchRepentogonUpdates(doc, allowPreReleases);
		if (fetchResult != Github::RELEASE_INFO_OK) {
			std::string filePath;
			Logger::Error("RepentogonInstaller::CheckRepentogonUpdatesThread error while checking version, trying steam\n");
			if (SubscribeDownloadAndGetFile(3643104060, "versiontracker/version.txt", filePath)) { //3643104060 is dummy id for testing
				fs::path p(filePath);
				p = p.parent_path().parent_path();
				std::wofstream out("steamentrydir.txt"); //this is mainly for the updater to find later kind of hacky, but its the only way I could think of where the updater can use the steam entry when available without the steamapi (wont be able to make it available if it isnt tho)
				out << p.wstring();

				std::ifstream file(filePath);
				std::stringstream buffer;
				buffer << file.rdbuf();
				std::string buf = buffer.str();

				doc.SetObject();
				auto& alloc = doc.GetAllocator();
				rapidjson::Value key("name", alloc);
				rapidjson::Value val(buf.c_str(), alloc);
				
				doc.AddMember(key, val, alloc);
				if ((repentogon.GetState() == REPENTOGON_INSTALLATION_STATUS_NONE) || (repentogon.GetZHLVersion() != buffer.str())) {
					return Github::VERSION_CHECK_NEW;
				}
				else if (repentogon.GetZHLVersion() == buffer.str()){
					return Github::VERSION_CHECK_UTD;
				}
			}
			Logger::Error("RepentogonInstaller::CheckRepentogonUpdatesThread error while checking version through steam\n");
			return Github::VERSION_CHECK_ERROR;
		}

		if (repentogon.GetState() == REPENTOGON_INSTALLATION_STATUS_NONE) {
			return Github::VERSION_CHECK_NEW;
		}

		return Github::CheckUpdates(repentogon.GetZHLVersion().c_str(), "Repentogon", doc);
	}

	Github::ReleaseInfoResult RepentogonInstaller::FetchRepentogonUpdates(
		rapidjson::Document& response, bool allowPreReleases) {
		curl::RequestParameters request;
		request.maxSpeed = sCLI->CurlLimit();
		request.timeout = sCLI->CurlTimeout();
		request.serverTimeout = sCLI->CurlConnectTimeout();

		Github::GenerateGithubHeaders(request);

		if (!allowPreReleases) {
			request.url = RepentogonURL;
			std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> releasesDesc =
				Github::AsyncFetchReleaseInfo(request);
			std::optional<curl::DownloadStringDescriptor> descriptor =
				GithubToRepInstall<curl::DownloadStringDescriptor>(
					&releasesDesc->base.monitor, releasesDesc->result);

			if (!descriptor) {
				releasesDesc->base.cancel.store(true, std::memory_order_release);
				Logger::Info("[RepentogonInstaller] Cancel requested\n");
				return Github::RELEASE_INFO_CURL_ERROR;
			}

			return Github::ValidateReleaseInfo(*descriptor, response, nullptr);
		} else {
			request.url = RepentogonReleasesURL;
			curl::DownloadStringDescriptor allReleasesResult = curl::DownloadString(request,
				"repentogon releases information");
			if (allReleasesResult.result != curl::DOWNLOAD_STRING_OK) {
				Logger::Error("Unable to download list of all Repentogon releases: %d\n", allReleasesResult.result);
				request.url = RepentogonURL;

				std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> releasesDesc =
					Github::AsyncFetchReleaseInfo(request);
				std::optional<curl::DownloadStringDescriptor> descriptor =
					GithubToRepInstall<curl::DownloadStringDescriptor>(
						&releasesDesc->base.monitor, releasesDesc->result);

				if (!descriptor) {
					releasesDesc->base.cancel.store(true, std::memory_order_release);
					Logger::Info("[RepentogonInstaller] Cancel requested\n");
					return Github::RELEASE_INFO_CURL_ERROR;
				}

				return Github::ValidateReleaseInfo(*descriptor, response, nullptr);
			}

			rapidjson::Document asJson;
			asJson.Parse(allReleasesResult.string.c_str());

			if (asJson.HasParseError() || (allReleasesResult.string.find("API rate limit exceeded for") != std::string::npos)) {
				Logger::Error("Unable to parse list of all Repentogon releases as JSON. Defaulting to latest release\n");
				curl::DownloadMonitor monitor;
				request.url = RepentogonURL;

				std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> releasesDesc =
					Github::AsyncFetchReleaseInfo(request);
				std::optional<curl::DownloadStringDescriptor> descriptor =
					GithubToRepInstall<curl::DownloadStringDescriptor>(
						&releasesDesc->base.monitor, releasesDesc->result);

				if (!descriptor) {
					releasesDesc->base.cancel.store(true, std::memory_order_release);
					Logger::Info("[RepentogonInstaller] Cancel requested\n");
					return Github::RELEASE_INFO_CURL_ERROR;
				}

				return Github::ValidateReleaseInfo(*descriptor, response, nullptr);
			}

			auto releases = asJson.GetArray();
			if (releases.Empty()) {
				Logger::Error("Trying to download a new Repentogon release, but none available\n");
				return Github::RELEASE_INFO_JSON_ERROR;
			}

			if (!releases[0].HasMember("url") || !releases[0]["url"].IsString()) {
				Logger::Error("Trying to download a new Repentogon release, but you ran out of requests! (try again next hour!)\n");
				return Github::RELEASE_INFO_JSON_ERROR;
			}

			curl::DownloadMonitor monitor;
			request.url = releases[0]["url"].GetString();
			std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> releasesDesc =
				Github::AsyncFetchReleaseInfo(request);
			std::optional<curl::DownloadStringDescriptor> descriptor =
				GithubToRepInstall<curl::DownloadStringDescriptor>(
					&releasesDesc->base.monitor, releasesDesc->result);

			if (!descriptor) {
				releasesDesc->base.cancel.store(true, std::memory_order_release);
				Logger::Info("[RepentogonInstaller] Cancel requested\n");
				return Github::RELEASE_INFO_CURL_ERROR;
			}

			return Github::ValidateReleaseInfo(*descriptor, response, nullptr);
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