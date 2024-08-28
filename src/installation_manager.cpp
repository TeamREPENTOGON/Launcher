#include <future>

#include "launcher/installation_manager.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

namespace Launcher {
	InstallationManager::InstallationManager(ILoggableGUI* gui) : _gui(gui) {

	}

	InstallationManager::SelfUpdateResult InstallationManager::HandleSelfUpdateResult(SelfUpdateErrorCode const& updateResult) {
		std::ostringstream err, info;
		bool isError = updateResult.base != SELF_UPDATE_UP_TO_DATE;
		bool timedout = updateResult.base == SELF_UPDATE_SELF_UPDATE_FAILED &&
			updateResult.detail.runUpdateResult == SELF_UPDATE_RUN_UPDATER_ERR_WAIT_TIMEOUT;
		switch (updateResult.base) {
		case SELF_UPDATE_UPDATE_CHECK_FAILED:
			err << "Error while checking for available launcher updates: ";
			switch (updateResult.detail.fetchUpdatesResult) {
			case Github::DOWNLOAD_AS_STRING_BAD_CURL:
				err << "error while initializing cURL";
				break;

			case Github::DOWNLOAD_AS_STRING_BAD_REQUEST:
				err << "error while performing cURL request";
				break;

			case Github::DOWNLOAD_AS_STRING_INVALID_JSON:
				err << "malformed HTTP answer";
				break;

			case Github::DOWNLOAD_AS_STRING_NO_NAME:
				err << "HTTP answer lacks a \"name\" field";
				break;
			}
			break;

		case SELF_UPDATE_EXTRACTION_FAILED:
			err << "Error while extracting the self-updater: ";
			switch (updateResult.detail.extractionResult) {
			case SELF_UPDATE_EXTRACTION_ERR_RESOURCE_NOT_FOUND:
				err << "unable to locate self-updater inside the launcher";
				break;

			case SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOAD_FAILED:
				err << "unable to load self-updater from the launcher";
				break;

			case SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOCK_FAILED:
				err << "unable to acquire resource lock on self-updater";
				break;

			case SELF_UPDATE_EXTRACTION_ERR_BAD_RESOURCE_SIZE:
				err << "embedded self-updater has the wrong size";
				break;

			case SELF_UPDATE_EXTRACTION_ERR_OPEN_TEMPORARY_FILE:
				err << "unable to open temporary file to extract self-updater";
				break;

			case SELF_UPDATE_EXTRACTION_ERR_WRITTEN_SIZE:
				err << "unable to write self-updater on the disk";
				break;
			}
			break;

		case SELF_UPDATE_SELF_UPDATE_FAILED:
			err << "Error while launching self-updater: ";
			switch (updateResult.detail.runUpdateResult) {
			case SELF_UPDATE_RUN_UPDATER_ERR_OPEN_LOCK_FILE:
				err << "error while opening internal lock file";
				break;

			case SELF_UPDATE_RUN_UPDATER_ERR_GENERATE_CLI:
				err << "error while generating self-updater command line";
				break;

			case SELF_UPDATE_RUN_UPDATER_ERR_CREATE_PROCESS:
				err << "error while creating self-updater process";
				break;

			case SELF_UPDATE_RUN_UPDATER_ERR_OPEN_PROCESS:
				err << "error while opening self-updater process handle";
				break;

			case SELF_UPDATE_RUN_UPDATER_ERR_WAIT:
				err << "error while waiting for self-updater ready";
				break;

			case SELF_UPDATE_RUN_UPDATER_ERR_WAIT_TIMEOUT:
				Logger::Error("Self-updater timed out while waiting availability\n");
				err << "timedout while waiting for self-updater ready";
				break;

			case SELF_UPDATE_RUN_UPDATER_ERR_INVALID_WAIT:
				err << "attempted to resume an update that was not started";
				break;
			}
			break;

		case SELF_UPDATE_UP_TO_DATE:
			info << "Everything up-to-date (please report this as a bug: forced updates should never display this message)";
			break;

		default:
			_gui->LogError("Unknown error %d while attempting self update", (int)updateResult.base);
			break;
		}

		if (isError) {
			_gui->LogError("%s", err.str().c_str());
		}
		else {
			_gui->Log("%s", info.str().c_str());
		}

		if (!timedout) {
			return isError ? SELF_UPDATE_ERR : SELF_UPDATE_UTD;
		}

		return SELF_UPDATE_PARTIAL;
	}

	bool InstallationManager::ResumeSelfUpdate(int currentRetry, int maxRetries) {
		SelfUpdateErrorCode resumeResult = _selfUpdater.ResumeSelfUpdate();
		if (resumeResult.base != SELF_UPDATE_SELF_UPDATE_FAILED) {
			_gui->LogError("Unexpected error category %d when retrying self-update, aborting", resumeResult.base);
			return false;
		}

		switch (resumeResult.detail.runUpdateResult) {
		case SELF_UPDATE_RUN_UPDATER_ERR_WAIT_TIMEOUT:
			_gui->LogError("Timedout while waiting for self-updater ready (retry %d/%d)", currentRetry, maxRetries);
			return true;

		case SELF_UPDATE_RUN_UPDATER_ERR_INVALID_WAIT:
			_gui->LogError("Attempted to resume an update that was not started");
			return false;

		default:
			_gui->LogError("Unexpected self-update error %d when retrying self-update, aborting", resumeResult.detail.runUpdateResult);
			return false;
		}

		return false;
	}

	void InstallationManager::InitFolders(bool* needIsaacFolder, bool* canWriteConfiguration) {
		Launcher::fs::IsaacInstallationPathInitResult initState = _installation.InitFolders();
		*needIsaacFolder = true;
		*canWriteConfiguration = false;
		bool foundConfiguration = false, couldOpenConfiguration = false, couldReadConfiguration = false;
		switch (initState) {
		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_PROFILE_DIR:
			_gui->LogError("[Configuration load] Unable to access the Repentance save folder");
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR:
			_gui->LogError("[Configuration load] No Repentance save folder found, please launch the vanilla game at least once");
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_NO_CONFIG:
			_gui->Log("[Configuration load] Found Isaac save folder %s", _installation.GetSaveFolder().c_str());
			_gui->LogWarn("[Configuration load] No Repentogon configuration file found in the Repentance save folder");
			*canWriteConfiguration = true;
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_OPEN_CONFIG:
			_gui->LogError("[Configuration load] Error while opening Repentogon configuration file %s", _installation.GetLauncherConfigurationPath().c_str());
			*canWriteConfiguration = foundConfiguration = true;
			break;

		case Launcher::fs::INSTALLATION_PATH_INIT_ERR_PARSE_CONFIG:
			_gui->LogError("[Configuration load] Error while processing Repentogon configuration file %s, syntax error on line %d\n",
				_installation.GetLauncherConfigurationPath().c_str(),
				_installation.GetConfigurationFileSyntaxErrorLine());
			*canWriteConfiguration = foundConfiguration = couldOpenConfiguration = true;
			break;

		default:
			*canWriteConfiguration = foundConfiguration = couldOpenConfiguration = couldReadConfiguration = true;
			*needIsaacFolder = false;
			break;
		}

		if (foundConfiguration && couldOpenConfiguration && couldReadConfiguration) {
			_gui->Log("[Configuration load] Found valid Repentogon configuration file %s", _installation.GetLauncherConfigurationPath().c_str());
			_gui->Log("[Configuration load] Found Isaac installation folder from configuration file: %s", _installation.GetIsaacInstallationFolder());
		}
		else {
			_gui->Log("[Configuration load] No valid Repentogon configuration file found, prompting user for Isaac installation folder...");
		}
	}

	bool InstallationManager::CheckIsaacInstallation() {
		_gui->LogNoNL("Checking Isaac installation... ");
		if (!_installation.CheckIsaacInstallation()) {
			_gui->Log("KO");
			_gui->LogWarn("[Isaac installation check] Unable to find isaac-ng.exe");
			return false;
		}
		else {
			_gui->Log("OK");
		}

		return true;
	}

	InstallationManager::CheckSelfUpdateResult InstallationManager::CheckSelfUpdates(bool allowPreReleases) {
		_gui->LogNoNL("Checking for availability of launcher updates... ");
		rapidjson::Document launcherResponse;
		CheckSelfUpdateResult result;
		Github::DownloadAsStringResult downloadReleasesResult;
		if (_selfUpdater.IsSelfUpdateAvailable(allowPreReleases, false, result.version, result.url, &downloadReleasesResult)) {
			_gui->Log("OK");
			_gui->Log("New version of the launcher available: %s (can be downloaded from %s)\n", result.version.c_str(), result.url.c_str());
			result.code = SELF_UPDATE_CHECK_NEW;;
		}
		else {
			_gui->Log("KO");
			if (downloadReleasesResult != Github::DOWNLOAD_AS_STRING_OK) {
				_gui->LogError("Error encountered while checking for availability of launcher update");
				switch (downloadReleasesResult) {
				case Github::DOWNLOAD_AS_STRING_BAD_CURL:
					_gui->LogError("Unable to initialize cURL connection");
					break;

				case Github::DOWNLOAD_AS_STRING_BAD_REQUEST:
					_gui->LogError("Unable to perform cURL request");
					break;

				case Github::DOWNLOAD_AS_STRING_INVALID_JSON:
					_gui->LogError("Invalid response");
					break;

				case Github::DOWNLOAD_AS_STRING_NO_NAME:
					_gui->LogError("Release has no \"name\" field (although you should not be seeing this error, report it as a bug");
					break;

				default:
					_gui->LogError("Unexpected error %d: report it as a bug", downloadReleasesResult);
					break;
				}

				result.code = SELF_UPDATE_CHECK_ERR;
			}
			else {
				result.code = SELF_UPDATE_CHECK_UTD;
			}
		}

		return result;
	}

	bool InstallationManager::CheckIsaacVersion() {
		_gui->LogNoNL("Checking Isaac version... ");
		fs::Version const* version = _installation.GetIsaacVersion();
		if (!version) {
			_gui->Log("KO");
			_gui->LogError("[Isaac version check] Unknown Isaac version. REPENTOGON will not launch.");
			return false;
		}

		_gui->Log("\n");
		_gui->LogNoNL("[Isaac version check] Identified Isaac version %s", version->version);
		if (!version->valid) {
			_gui->Log("\n");
			_gui->LogError("[Isaac version check] This version of the game does not support REPENTOGON.");
		}
		else {
			_gui->Log(": this version of the game is compatible with REPENTOGON.");
		}

		return version->valid;
	}

	bool InstallationManager::UninstallLegacyRepentogon() {
		_gui->Log("Uninstalling legacy Repentogon installation...");

		std::string dsound = _installation.GetIsaacInstallationFolder() + "/dsound.dll";
		std::string zhl = _installation.GetIsaacInstallationFolder() + "/libzhl.dll";
		std::string repentogon = _installation.GetIsaacInstallationFolder() + "/zhlRepentogon.dll";

		bool dsoundOk = RemoveFile(dsound.c_str()),
			zhlOk = RemoveFile(zhl.c_str()),
			repentogonOk = RemoveFile(repentogon.c_str());
		return dsoundOk && zhlOk && repentogonOk;
	}

	bool InstallationManager::RemoveFile(const char* filename) {
		if (!Filesystem::FileExists(filename)) {
			return true;
		}

		_gui->LogNoNL("Removing file %s... ", filename);
		bool ok = Filesystem::RemoveFile(filename);
		if (ok) {
			_gui->Log("OK");
		}
		else {
			_gui->Log("KO");
			_gui->LogError("Error while deleting file %s (GetLastError() = %d)\n", filename, GetLastError());
		}

		return ok;
	}
	
	InstallationManager::RepentogonInstallationCheckResult InstallationManager::ManageLegacyInstallation() {
		_gui->LogWarn("Found a valid legacy installation of Repentogon (dsound.dll is present). Removing legacy files and updating\n");
		if (!UninstallLegacyRepentogon()) {
			if (Filesystem::FileExists((_installation.GetIsaacInstallationFolder() + "/dsound.dll").c_str())) {
				_gui->LogError("Errors encountered while removing legacy Repentogon installation");
				_gui->LogError("%s/dsound.dll still exists: please remove it manually",
					_installation.GetIsaacInstallationFolder().c_str());
				return REPENTOGON_INSTALLATION_CHECK_KO_LEGACY;
			}
			else {
				_gui->LogWarn("Errors encountered while removing legacy Repentogon installation");
				_gui->LogWarn("%s/dsound.dll was removed: you may safely launch Repentogon from the launcher",
					_installation.GetIsaacInstallationFolder().c_str());
				// Return KO to prompt the installation of newer Repentogon
				return REPENTOGON_INSTALLATION_CHECK_KO;
			}
		}
		else {
			_gui->Log("Successfully uninstalled legacy Repentogon installation");
			// Return KO to prompt the installation of newer Repentogon
			return REPENTOGON_INSTALLATION_CHECK_KO;
		}
	}

	InstallationManager::RepentogonInstallationCheckResult InstallationManager::CheckRepentogonInstallation(bool isRetry, bool isUpdate) {
		_gui->Log("Checking Repentogon installation...");
		if (_installation.CheckRepentogonInstallation()) {
			if (!isUpdate) {
				_zhlVersion = _installation.GetZHLVersion();
				_repentogonVersion = _installation.GetRepentogonVersion();
			}

			if (_installation.GetRepentogonInstallationState() == fs::REPENTOGON_INSTALLATION_STATE_LEGACY) {
				if (isRetry || isUpdate) {
					_gui->LogWarn("Newly installed version of Repentogon is a legacy (dsound.dll based) installation.\n");
					_gui->LogWarn("This may indicate a broken release process on Repentogon's side. Please check with the devs.\n");
					DisplayRepentogonFilesVersion(1, isUpdate);
					return REPENTOGON_INSTALLATION_CHECK_LEGACY;
				}
				else {
					return ManageLegacyInstallation();
				}
			}
			else {
				if (isRetry) {
					_gui->Log("Repentogon was successfully installed: ");
				}
				else if (isUpdate) {
					_gui->Log("Repentogon was successfully updated: ");
				}
				else {
					_gui->Log("Found a valid Repentogon installation: ");
				}

				DisplayRepentogonFilesVersion(1, isUpdate);
			}

			return REPENTOGON_INSTALLATION_CHECK_OK;
		}
		else {
			DebugDumpBrokenRepentogonInstallation();
			return REPENTOGON_INSTALLATION_CHECK_KO;
		}
	}

	void InstallationManager::DisplayRepentogonFilesVersion(int tabs, bool isUpdate) {
		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("ZHL version: %s", _installation.GetZHLVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _zhlVersion.c_str());
		}
		else {
			_gui->Log("\n");
		}

		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("Repentogon version: %s", _installation.GetRepentogonVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _repentogonVersion.c_str());
		}
		else {
			_gui->Log("\n");
		}
	}

	void InstallationManager::DebugDumpBrokenRepentogonInstallation() {
		_gui->Log("Found no valid installation of Repentogon");
		_gui->Log("\tRequired DLLs found / not found:");
		std::vector<fs::FoundFile> const& files = _installation.GetRepentogonInstallationFilesState();
		for (fs::FoundFile const& file : files) {
			if (file.found) {
				_gui->Log("\t\t%s: found", file.filename.c_str());
			}
			else {
				_gui->Log("\t\t%s: not found", file.filename.c_str());
			}
		}

		bool zhlVersionAvailable = false, repentogonVersionAvailable = false;
		_gui->LogNoNL("\tLoad status of the ZHL DLL (libzhl.dll): ");
		if (_installation.WasLibraryLoaded(fs::LOADABLE_DLL_ZHL)) {
			std::string const& zhlVersion = _installation.GetZHLVersion();
			if (zhlVersion.empty()) {
				_gui->Log("unable to find version");
			}
			else {
				zhlVersionAvailable = true;
				_gui->Log("found version %s", zhlVersion.c_str());
			}
		}
		else {
			_gui->Log("unable to load");
		}

		_gui->LogNoNL("\tLoad status of the Repentogon DLL (zhlRepentogon.dll): ");
		if (_installation.WasLibraryLoaded(fs::LOADABLE_DLL_REPENTOGON)) {
			std::string const& repentogonVersion = _installation.GetRepentogonVersion();
			if (repentogonVersion.empty()) {
				_gui->Log("unable to find version");
			}
			else {
				repentogonVersionAvailable = true;
				_gui->Log("found version %s", repentogonVersion.c_str());
			}
		}
		else {
			_gui->Log("unable to load");
		}

		if (zhlVersionAvailable && repentogonVersionAvailable) {
			if (_installation.RepentogonZHLVersionMatch()) {
				_gui->Log("\tZHL / Repentogon version match");
			}
			else {
				_gui->Log("\tZHL / Repentogon version mismatch");
			}
		}
	}

	bool InstallationManager::InstallRepentogon(rapidjson::Document& response) {
		_gui->Log("Downloading Repentogon release %s...", response["name"].GetString());
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::future<RepentogonUpdateResult> result = std::async(std::launch::async,
			&Launcher::Updater::UpdateRepentogon, &_updater, std::ref(response), _installation.GetIsaacInstallationFolder().c_str(), &monitor);
		size_t totalDownloadSize = 0;
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
			lastReceived = now;
		while (result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
			while (std::optional<Github::GithubDownloadNotification> message = monitor.Get()) {
				switch (message->type) {
				case Github::GH_NOTIFICATION_INIT_CURL:
					_gui->Log("[Updater] Initializing cURL connection to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_INIT_CURL_DONE:
					_gui->Log("[Updater] Initialized cURL connection to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_CURL_PERFORM:
					_gui->Log("[Updater] Performing cURL request to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_CURL_PERFORM_DONE:
					_gui->Log("[Updater] Performed cURL request to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_DATA_RECEIVED:
					totalDownloadSize += std::get<size_t>(message->data);

					now = std::chrono::steady_clock::now();
					if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReceived).count() > 100) {
						lastReceived = now;
						_gui->Log("[Updater] Downloaded %lu bytes", totalDownloadSize);
					}
					break;

				case Github::GH_NOTIFICATION_PARSE_RESPONSE:
					_gui->Log("[Updater] Parsing result of cURL request from %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_PARSE_RESPONSE_DONE:
					_gui->Log("[Updater] Parsed result of cURL request from %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_DONE:
					_gui->Log("[Updater] Successfully downloaded content from %s", std::get<std::string>(message->data).c_str());
					break;

				default:
					_gui->LogError("[Updater] Unexpected asynchronous notification (id = %d)", message->type);
					break;
				}
			}
		}

		RepentogonUpdateState const& state = _updater.GetRepentogonUpdateState();
		Launcher::RepentogonUpdateResult updateResult = result.get();
		switch (updateResult) {
		case REPENTOGON_UPDATE_RESULT_MISSING_ASSET:
			_gui->LogError("Could not install Repentogon: bad assets in release information\n");
			_gui->LogError("Found hash.txt: %s\n", state.hashOk ? "yes" : "no");
			_gui->LogError("Found REPENTOGON.zip: %s\n", state.zipOk ? "yes" : "no");
			break;

		case REPENTOGON_UPDATE_RESULT_DOWNLOAD_ERROR:
			_gui->LogError("Could not install Repentogon: download error\n");
			_gui->LogError("Downloaded hash.txt: %s\n", state.hashFile ? "yes" : "no");
			_gui->LogError("Downloaded REPENTOGON.zip: %s\n", state.zipFile ? "yes" : "no");
			break;

		case REPENTOGON_UPDATE_RESULT_BAD_HASH:
			_gui->LogError("Could not install Repentogon: bad archive hash\n");
			_gui->LogError("Expected hash \"%s\", got \"%s\"\n", state.hash.c_str(), state.zipHash.c_str());
			break;

		case REPENTOGON_UPDATE_RESULT_EXTRACT_FAILED:
		{
			int i = 0;
			_gui->LogError("Could not install Repentogon: error while extracting archive\n");
			for (auto const& [filename, success] : state.unzipedFiles) {
				if (filename.empty()) {
					_gui->LogError("Could not extract file %d from the archive\n", i);
				}
				else {
					_gui->LogError("Extracted %s: %s\n", filename.c_str(), success ? "yes" : "no");
				}
			}
			break;
		}

		case REPENTOGON_UPDATE_RESULT_OK:
			_gui->Log("Successfully installed latest Repentogon release\n");
			break;

		default:
			_gui->LogError("Unknown error code from Updater::UpdateRepentogon: %d\n", updateResult);
		}

		return updateResult == REPENTOGON_UPDATE_RESULT_OK;
	}

	InstallationManager::DownloadInstallRepentogonResult InstallationManager::InstallLatestRepentogon(bool force, bool allowPreReleases) {
		rapidjson::Document document;
		if (!CheckRepentogonUpdates(document, allowPreReleases, force)) {
			return DOWNLOAD_INSTALL_REPENTOGON_UTD;
		}

		return InstallRepentogon(document) ? DOWNLOAD_INSTALL_REPENTOGON_OK : DOWNLOAD_INSTALL_REPENTOGON_ERR;
	}

	bool InstallationManager::CheckRepentogonUpdates(rapidjson::Document& document,
		bool allowPreReleases, bool force) {
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		Github::VersionCheckResult result = _updater.CheckRepentogonUpdates(document, _installation, allowPreReleases, &monitor);
		if (result == Github::VERSION_CHECK_NEW)
			return true;

		if (result == Github::VERSION_CHECK_UTD)
			return force;

		return false;
	}
}