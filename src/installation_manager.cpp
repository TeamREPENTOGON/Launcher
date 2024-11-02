#include <future>

#include "launcher/installation_manager.h"
#include "launcher/version.h"
#include "launcher/self_updater/finalizer.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

namespace Launcher {
	InstallationManager::InstallationManager(ILoggableGUI* gui) : _gui(gui), _installation(gui) {

	}

	void InstallationManager::InitFolders(bool* needIsaacFolder, bool* canWriteConfiguration) {
		Launcher::IsaacInstallationPathInitResult initState = _installation.InitFolders();
		*needIsaacFolder = true;
		*canWriteConfiguration = false;
		bool foundConfiguration = false, couldOpenConfiguration = false, couldReadConfiguration = false;
		switch (initState) {
		case Launcher::INSTALLATION_PATH_INIT_ERR_PROFILE_DIR:
			_gui->LogError("[Configuration load] Unable to access the Repentance save folder");
			break;

		case Launcher::INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR:
			_gui->LogError("[Configuration load] No Repentance save folder found, please launch the vanilla game at least once");
			break;

		case Launcher::INSTALLATION_PATH_INIT_ERR_NO_CONFIG:
			_gui->Log("[Configuration load] Found Isaac save folder %s", _installation.GetSaveFolder().c_str());
			_gui->LogWarn("[Configuration load] No Repentogon configuration file found in the Repentance save folder");
			*canWriteConfiguration = true;
			break;

		case Launcher::INSTALLATION_PATH_INIT_ERR_OPEN_CONFIG:
			_gui->LogError("[Configuration load] Error while opening Repentogon configuration file %s", _installation.GetLauncherConfigurationPath().c_str());
			*canWriteConfiguration = foundConfiguration = true;
			break;

		case Launcher::INSTALLATION_PATH_INIT_ERR_PARSE_CONFIG:
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
		} else {
			_gui->Log("[Configuration load] No valid Repentogon configuration file found, prompting user for Isaac installation folder...");
		}
	}

	bool InstallationManager::CheckIsaacInstallation() {
		_gui->LogNoNL("Checking Isaac installation... ");
		if (!_installation.CheckIsaacInstallation()) {
			_gui->Log("KO");
			_gui->LogWarn("[Isaac installation check] Unable to find isaac-ng.exe");
			return false;
		} else {
			_gui->Log("OK");
		}

		return true;
	}

	bool InstallationManager::CheckIsaacVersion() {
		_gui->LogNoNL("Checking Isaac version... ");
		Version const* version = _installation.GetIsaacVersion();
		if (!version) {
			

			_gui->Log("KO");
			_gui->LogError("[Isaac version check] Unknown Isaac version. REPENTOGON will not launch.");
			return false;
		} else {
			_gui->Log("\n");
			_gui->LogNoNL("[Isaac version check] Identified Isaac version %s", version->version);
			if (!version->valid) {
				_gui->Log("\n");
				_gui->LogError("[Isaac version check] This version of the game does not support REPENTOGON.");
			} else {
				_gui->Log(": this version of the game is compatible with REPENTOGON.");
			}

			return version->valid;
		}
	}

	bool InstallationManager::UninstallLegacyRepentogon() {
		_gui->Log("Uninstalling legacy Repentogon installation...");

		std::string dsound = _installation.GetIsaacInstallationFolder() + "/" + "dsound.dll";
		std::string zhl = _installation.GetIsaacInstallationFolder() + "/" + Libraries::zhl;
		std::string repentogon = _installation.GetIsaacInstallationFolder() + "/" + Libraries::repentogon;

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
		} else {
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
				return GetSetRepentogonInstallationState(REPENTOGON_INSTALLATION_CHECK_KO_LEGACY);
			} else {
				_gui->LogWarn("Errors encountered while removing legacy Repentogon installation");
				_gui->LogWarn("%s/dsound.dll was removed: you may safely launch Repentogon from the launcher",
					_installation.GetIsaacInstallationFolder().c_str());
				// Return KO to prompt the installation of newer Repentogon
				return GetSetRepentogonInstallationState(REPENTOGON_INSTALLATION_CHECK_KO);
			}
		} else {
			_gui->Log("Successfully uninstalled legacy Repentogon installation");
			// Return KO to prompt the installation of newer Repentogon
			return GetSetRepentogonInstallationState(REPENTOGON_INSTALLATION_CHECK_KO);
		}
	}

	InstallationManager::RepentogonInstallationCheckResult InstallationManager::CheckRepentogonInstallation(bool isRetry, bool isUpdate) {
		_gui->Log("Checking Repentogon installation...");
		if (_installation.CheckRepentogonInstallation()) {
			if (!isUpdate) {
				_zhlVersion = _installation.GetZHLVersion();
				_repentogonVersion = _installation.GetRepentogonVersion();
				_zhlLoaderVersion = _installation.GetZHLLoaderVersion();
			}

			if (_installation.GetRepentogonInstallationState() == REPENTOGON_INSTALLATION_STATE_LEGACY) {
				if (isRetry || isUpdate) {
					_gui->LogWarn("Newly installed version of Repentogon is a legacy (dsound.dll based) installation.\n");
					_gui->LogWarn("This may indicate a broken release process on Repentogon's side. Please check with the devs.\n");
					DisplayRepentogonFilesVersion(1, isUpdate);
					return GetSetRepentogonInstallationState(REPENTOGON_INSTALLATION_CHECK_LEGACY);
				} else {
					return GetSetRepentogonInstallationState(ManageLegacyInstallation());
				}
			} else {
				if (isRetry) {
					_gui->Log("Repentogon was successfully installed: ");
				} else if (isUpdate) {
					_gui->Log("Repentogon was successfully updated: ");
				} else {
					_gui->Log("Found a valid Repentogon installation: ");
				}

				DisplayRepentogonFilesVersion(1, isUpdate);
			}

			return GetSetRepentogonInstallationState(REPENTOGON_INSTALLATION_CHECK_OK);
		} else {
			DebugDumpBrokenRepentogonInstallation();
			return GetSetRepentogonInstallationState(REPENTOGON_INSTALLATION_CHECK_KO);
		}
	}

	void InstallationManager::DisplayRepentogonFilesVersion(int tabs, bool isUpdate) {
		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("ZHL version: %s", _installation.GetZHLVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _zhlVersion.c_str());
		} else {
			_gui->Log("\n");
		}

		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("ZHL loader version: %s", _installation.GetZHLLoaderVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _zhlVersion.c_str());
		} else {
			_gui->Log("\n");
		}

		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("Repentogon version: %s", _installation.GetRepentogonVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _repentogonVersion.c_str());
		} else {
			_gui->Log("\n");
		}
	}

	void InstallationManager::DebugDumpBrokenRepentogonInstallationDLL(const char* context, const char* libname, LoadableDlls dll,
		std::string const& (Installation::* ptr)() const, bool* found) {
		_gui->LogNoNL("\tLoad status of %s (%s): ", context, libname);
		if (_installation.WasLibraryLoaded(dll)) {
			std::string const& version = (_installation.*ptr)();
			if (version.empty()) {
				_gui->Log("unable to find version");
			} else {
				*found = true;
				_gui->Log("found version %s", version.c_str());
			}
		} else {
			_gui->Log("unable to load");
		}
	}

	void InstallationManager::DebugDumpBrokenRepentogonInstallation() {
		_gui->Log("Found no valid installation of Repentogon");
		_gui->Log("\tRequired files found / not found:");
		std::vector<FoundFile> const& files = _installation.GetRepentogonInstallationFilesState();
		for (FoundFile const& file : files) {
			if (file.found) {
				_gui->Log("\t\t%s: found", file.filename.c_str());
			} else {
				_gui->Log("\t\t%s: not found", file.filename.c_str());
			}
		}

		bool zhlVersionAvailable = false, repentogonVersionAvailable = false, zhlLoaderVersionAvailable = false;
		DebugDumpBrokenRepentogonInstallationDLL("the ZHL DLL",			Libraries::zhl,			LOADABLE_DLL_LIBZHL,		&Installation::GetZHLVersion,			&zhlVersionAvailable);
		DebugDumpBrokenRepentogonInstallationDLL("the ZHL loader DLL",	Libraries::loader,		LOADABLE_DLL_ZHL_LOADER,	&Installation::GetZHLLoaderVersion,		&zhlLoaderVersionAvailable);
		DebugDumpBrokenRepentogonInstallationDLL("the Repentogon DLL",	Libraries::repentogon,	LOADABLE_DLL_REPENTOGON,	&Installation::GetRepentogonVersion,	&repentogonVersionAvailable);

		if (zhlVersionAvailable && repentogonVersionAvailable && zhlLoaderVersionAvailable) {
			if (_installation.RepentogonZHLVersionMatch()) {
				_gui->Log("\tZHL / Repentogon version match");
			} else {
				_gui->Log("\tZHL / Repentogon version mismatch");
			}
		}
	}

	bool InstallationManager::InstallRepentogon(rapidjson::Document& response) {
		_gui->Log("Downloading Repentogon release %s...", response["name"].GetString());
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::future<RepentogonUpdateResult> result = std::async(std::launch::async,
			&Launcher::RepentogonUpdater::UpdateRepentogon, &_updater, std::ref(response), _installation.GetIsaacInstallationFolder().c_str(), &monitor);
		size_t totalDownloadSize = 0;
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
			lastReceived = now;
		while (result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
			while (std::optional<Github::GithubDownloadNotification> message = monitor.Get()) {
				switch (message->type) {
				case Github::GH_NOTIFICATION_INIT_CURL:
					_gui->Log("[RepentogonUpdater] Initializing cURL connection to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_INIT_CURL_DONE:
					_gui->Log("[RepentogonUpdater] Initialized cURL connection to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_CURL_PERFORM:
					_gui->Log("[RepentogonUpdater] Performing cURL request to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_CURL_PERFORM_DONE:
					_gui->Log("[RepentogonUpdater] Performed cURL request to %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_DATA_RECEIVED:
					totalDownloadSize += std::get<size_t>(message->data);

					now = std::chrono::steady_clock::now();
					if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReceived).count() > 100) {
						lastReceived = now;
						_gui->Log("[RepentogonUpdater] Downloaded %lu bytes", totalDownloadSize);
					}
					break;

				case Github::GH_NOTIFICATION_PARSE_RESPONSE:
					_gui->Log("[RepentogonUpdater] Parsing result of cURL request from %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_PARSE_RESPONSE_DONE:
					_gui->Log("[RepentogonUpdater] Parsed result of cURL request from %s", std::get<std::string>(message->data).c_str());
					break;

				case Github::GH_NOTIFICATION_DONE:
					_gui->Log("[RepentogonUpdater] Successfully downloaded content from %s", std::get<std::string>(message->data).c_str());
					break;

				default:
					_gui->LogError("[RepentogonUpdater] Unexpected asynchronous notification (id = %d)", message->type);
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
				} else {
					_gui->LogError("Extracted %s: %s\n", filename.c_str(), success ? "yes" : "no");
				}
			}
			break;
		}

		case REPENTOGON_UPDATE_RESULT_OK:
			_gui->Log("Successfully installed latest Repentogon release\n");
			break;

		default:
			_gui->LogError("Unknown error code from RepentogonUpdater::UpdateRepentogon: %d\n", updateResult);
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