#include <future>

#include "launcher/installation_manager.h"
#include "launcher/version.h"
#include "launcher/self_updater/finalizer.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

namespace Launcher {
	InstallationManager::InstallationManager(ILoggableGUI* gui, Installation* installation) : _gui(gui), _installation(installation) {

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

	void InstallationManager::DisplayRepentogonFilesVersion(int tabs, bool isUpdate) {
		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("ZHL version: %s", _installation->GetZHLVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _zhlVersion.c_str());
		} else {
			_gui->Log("\n");
		}

		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("ZHL loader version: %s", _installation->GetZHLLoaderVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _zhlVersion.c_str());
		} else {
			_gui->Log("\n");
		}

		for (int i = 0; i < tabs; ++i) {
			_gui->LogNoNL("\t");
		}

		_gui->LogNoNL("Repentogon version: %s", _installation->GetRepentogonVersion().c_str());
		if (isUpdate) {
			_gui->Log(" (updated from %s)", _repentogonVersion.c_str());
		} else {
			_gui->Log("\n");
		}
	}

	void InstallationManager::DebugDumpBrokenRepentogonInstallationDLL(const char* context, const char* libname, LoadableDlls dll,
		std::string const& (Installation::* ptr)() const, bool* found) {
		_gui->LogNoNL("\tLoad status of %s (%s): ", context, libname);
		LoadDLLState loadState = _installation->GetDLLLoadState(dll);

		if (loadState == LOAD_DLL_STATE_OK) {
			std::string const& version = (_installation->*ptr)();
			if (version.empty()) {
				_gui->Log("unable to find version");
			} else {
				*found = true;
				_gui->Log("found version %s", version.c_str());
			}
		} else if (loadState == LOAD_DLL_STATE_FAIL) {
			_gui->Log("unable to load");
		} else {
			_gui->Log("no load attempted");
		}
	}

	void InstallationManager::DebugDumpBrokenRepentogonInstallation() {
		_gui->Log("Found no valid installation of Repentogon");
		_gui->Log("\tRequired files found / not found:");
		std::vector<FoundFile> const& files = _installation->GetRepentogonInstallationFilesState();
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
			if (_installation->RepentogonZHLVersionMatch()) {
				_gui->Log("\tZHL / Repentogon version match");
			} else {
				_gui->Log("\tZHL / Repentogon version mismatch");
			}
		}
	}

	bool InstallationManager::InstallRepentogon(rapidjson::Document& response) {
		if (!HandleLegacyInstallation()) {
			_gui->LogError("Unable to manage dsound.dll, aborting Repentogon installation\n");
			return false;
		}

		_zhlVersion = _installation->GetZHLVersion();
		_repentogonVersion = _installation->GetRepentogonVersion();
		_loaderVersion = _installation->GetZHLLoaderVersion();

		Launcher::RepentogonUpdater updater;
		_gui->Log("Downloading Repentogon release %s...", response["name"].GetString());
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::future<RepentogonUpdateResult> result = std::async(std::launch::async,
			&Launcher::RepentogonUpdater::UpdateRepentogon, &updater, std::ref(response), _installation->GetIsaacInstallationFolder().c_str(), &monitor);
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

		RepentogonUpdateState const& state = updater.GetRepentogonUpdateState();
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

				++i;
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
		CheckRepentogonUpdatesResult checkUpdates = CheckRepentogonUpdates(document, allowPreReleases, force);
		if (checkUpdates == CHECK_REPENTOGON_UPDATES_UTD) {
			return DOWNLOAD_INSTALL_REPENTOGON_UTD;
		}

		if (checkUpdates == CHECK_REPENTOGON_UPDATES_ERR) {
			return DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES;
		}

		return InstallRepentogon(document) ? DOWNLOAD_INSTALL_REPENTOGON_OK : DOWNLOAD_INSTALL_REPENTOGON_ERR;
	}

	InstallationManager::CheckRepentogonUpdatesResult InstallationManager::CheckRepentogonUpdates(rapidjson::Document& document,
		bool allowPreReleases, bool force) {
		Launcher::RepentogonUpdater updater;
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		Github::VersionCheckResult result = updater.CheckRepentogonUpdates(document, *_installation, allowPreReleases, &monitor);
		if (result == Github::VERSION_CHECK_NEW)
			return CHECK_REPENTOGON_UPDATES_NEW;

		if (result == Github::VERSION_CHECK_UTD)
			return force ? CHECK_REPENTOGON_UPDATES_NEW : CHECK_REPENTOGON_UPDATES_UTD;

		return CHECK_REPENTOGON_UPDATES_ERR;
	}

	bool InstallationManager::HandleLegacyInstallation() {
		bool needRemoveDsoundDLL = false;
		std::string dsoundPath = _installation->GetIsaacInstallationFolder() + "\\dsound.dll";

		if (_installation->GetRepentogonInstallationState() == REPENTOGON_INSTALLATION_STATE_LEGACY) {
			_gui->Log("Current Repentogon installation is a legacy installation, removing dsound.dll\n");
			needRemoveDsoundDLL = true;
		} else if (_installation->GetRepentogonInstallationState() == REPENTOGON_INSTALLATION_STATE_NONE) {
			if (Filesystem::FileExists(dsoundPath.c_str())) {
				_gui->Log("Found dsound.dll in Isaac installation folder, removing it preemptively\n");
				needRemoveDsoundDLL = true;
			}
		}

		if (needRemoveDsoundDLL) {
			return RemoveFile(dsoundPath.c_str());
		}

		return true;
	}
}