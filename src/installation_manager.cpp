#include <future>

#include "launcher/installation_manager.h"
#include "launcher/version.h"
#include "launcher/self_updater/finalizer.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

namespace Launcher {
	InstallationManager::InstallationManager(ILoggableGUI* gui) : _gui(gui), _launcherUpdateManager(gui) {

	}

	void InstallationManager::HandleFinalizationResult(Updater::FinalizationResult const& finalizationResult) {
		struct ResultVisitor {
			ResultVisitor(std::ostringstream* info, std::ostringstream* err, std::ostringstream* warn,
				bool* isInfo, bool* isWarn) : info(info), err(err), warn(warn), 
				isInfo(isInfo), isWarn(isWarn) {

			}

			std::ostringstream *info, *err, *warn;
			bool *isWarn, *isInfo;

			void operator()(Updater::FinalizationExtractionResult code) {
				*err << "Error while extracting the unpacker: ";
				switch (code) {
				case Updater::FINALIZATION_EXTRACTION_ERR_RESOURCE_NOT_FOUND:
					*err << "unable to locate unpacker inside the launcher";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_RESOURCE_LOAD_FAILED:
					*err << "unable to load unpacker from the launcher";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_RESOURCE_LOCK_FAILED:
					*err << "unable to acquire resource lock on unpacker";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_BAD_RESOURCE_SIZE:
					*err << "embedded unpacker has the wrong size";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_OPEN_TEMPORARY_FILE:
					*err << "unable to open temporary file to extract unpacker";
					break;

				case Updater::FINALIZATION_EXTRACTION_ERR_WRITTEN_SIZE:
					*err << "unable to write self-updater on the disk";
					break;
				}
			}

			void operator()(Updater::FinalizationStartUnpackerResult code) {
				*err << "Error while launching unpacker: ";
				switch (code) {
				case Updater::FINALIZATION_START_UNPACKER_ERR_NO_PIPE:
					*err << "unable to create pipe to communicate with unpacker";
					break;

				case Updater::FINALIZATION_START_UNPACKER_ERR_CREATE_PROCESS:
					*err << "error while creating unpacker process";
					break;

				case Updater::FINALIZATION_START_UNPACKER_ERR_OPEN_PROCESS:
					*err << "error while opening unpacker process handle";
					break;

				default:
					*err << "Unknown error " << code << " while launching unpacker";
					break;
				}
			}

			void operator()(Updater::FinalizationCommunicationResult code) {
				switch (code) {
				case Updater::FINALIZATION_COMM_ERR_READFILE_ERROR:
					*err << "unknown error while checking for availability of self-updater";
					break;

				case Updater::FINALIZATION_COMM_ERR_INVALID_PING:
					*err << "invalid message sent by self-updater";
					break;

				case Updater::FINALIZATION_COMM_ERR_INVALID_RESUME:
					*err << "attempted to resume an update that was not started";
					break;

				case Updater::FINALIZATION_COMM_ERR_STILL_ALIVE:
					*err << "launcher was not properly terminated by updater";
					break;

				case Updater::FINALIZATION_COMM_INFO_TIMEOUT:
					*isWarn = true;
					*warn << "Timed out while waiting for availability of the self-updater";
					break;

				case Updater::FINALIZATION_COMM_INTERNAL_OK:
					*isInfo = true;
					*info << "Synchronizing the launcher and the updater...";
					break;
				}
			}
		};

		std::ostringstream info, err, warn;
		bool isInfo = false, isWarn = false;

		std::visit(ResultVisitor(&info, &err, &warn, &isInfo, &isWarn), finalizationResult);

		if (isInfo) {
			_gui->LogInfo(info.str().c_str());
		} else if (isWarn) {
			_gui->LogWarn(warn.str().c_str());
		} else {
			_gui->LogError(err.str().c_str());
		}
	}

	bool InstallationManager::HandleSelfUpdateResult(SelfUpdateErrorCode const& updateResult) {
		std::ostringstream err, info;
		bool isInfo = false;

		std::optional<Github::DownloadAsStringResult> downloadResult;
		std::optional<CandidateVersion> candidate;

		if (std::holds_alternative<Github::DownloadAsStringResult>(updateResult.detail)) {
			downloadResult = std::get<Github::DownloadAsStringResult>(updateResult.detail);
		} else {
			candidate = std::get<CandidateVersion>(updateResult.detail);
		}

		switch (updateResult.base) {
		case SELF_UPDATE_UPDATE_CHECK_FAILED:
			err << "Error while checking for available launcher updates: ";
			switch (*downloadResult) {
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

		case SELF_UPDATE_UP_TO_DATE:
			isInfo = true;
			info << "Everything up-to-date";
			break;

		case SELF_UPDATE_CANDIDATE:
			isInfo = true;
			info << "A candidate release has been found: " << candidate->version << ", available at " << candidate->url;
			break;

		default:
			_gui->LogError("Unknown error category %d while attempting self update", updateResult.base);
			break;
		}

		if (isInfo) {
			_gui->LogInfo(info.str().c_str());
		} else {
			_gui->LogError(err.str().c_str());
		}
		return updateResult.base != SELF_UPDATE_UPDATE_CHECK_FAILED;
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

	InstallationManager::CheckSelfUpdateResult InstallationManager::CheckSelfUpdates(bool allowPreReleases) {
		_gui->LogNoNL("Checking for availability of launcher updates... ");
		rapidjson::Document launcherResponse;
		CheckSelfUpdateResult result;
		Github::DownloadAsStringResult downloadReleasesResult;
		if (_selfUpdater.IsSelfUpdateAvailable(allowPreReleases, false, result.version, result.url, &downloadReleasesResult)) {
			_gui->Log("OK");
			_gui->Log("New version of the launcher available: %s (can be downloaded from %s)\n", result.version.c_str(), result.url.c_str());
			result.code = SELF_UPDATE_CHECK_NEW;;
		} else {
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
			} else {
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
		std::string zhl = _installation.GetIsaacInstallationFolder() + "/" + fs::Libraries::zhl;
		std::string repentogon = _installation.GetIsaacInstallationFolder() + "/" + fs::Libraries::repentogon;

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

			if (_installation.GetRepentogonInstallationState() == fs::REPENTOGON_INSTALLATION_STATE_LEGACY) {
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

	void InstallationManager::DebugDumpBrokenRepentogonInstallationDLL(const char* context, const char* libname, fs::LoadableDlls dll,
		std::string const& (fs::Installation::* ptr)() const, bool* found) {
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
		std::vector<fs::FoundFile> const& files = _installation.GetRepentogonInstallationFilesState();
		for (fs::FoundFile const& file : files) {
			if (file.found) {
				_gui->Log("\t\t%s: found", file.filename.c_str());
			} else {
				_gui->Log("\t\t%s: not found", file.filename.c_str());
			}
		}

		bool zhlVersionAvailable = false, repentogonVersionAvailable = false, zhlLoaderVersionAvailable = false;
		DebugDumpBrokenRepentogonInstallationDLL("the ZHL DLL",			fs::Libraries::zhl,			fs::LOADABLE_DLL_LIBZHL,		&fs::Installation::GetZHLVersion,			&zhlVersionAvailable);
		DebugDumpBrokenRepentogonInstallationDLL("the ZHL loader DLL",	fs::Libraries::loader,		fs::LOADABLE_DLL_ZHL_LOADER,	&fs::Installation::GetZHLLoaderVersion,		&zhlLoaderVersionAvailable);
		DebugDumpBrokenRepentogonInstallationDLL("the Repentogon DLL",	fs::Libraries::repentogon,	fs::LOADABLE_DLL_REPENTOGON,	&fs::Installation::GetRepentogonVersion,	&repentogonVersionAvailable);

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

	void InstallationManager::DoSelfUpdate(std::string const& version, std::string const& url) {
		if (!_launcherUpdateManager.DoUpdate(Launcher::version, version.c_str(), url.c_str())) {
			return;
		}

		FinishSelfUpdate();
	}

	void InstallationManager::ForceSelfUpdate(bool unstable) {
		_gui->Log("Performing self-update (forcibly triggered)");
		Launcher::SelfUpdateErrorCode result = _selfUpdater.SelectReleaseTarget(unstable, true);
		if (result.base != SELF_UPDATE_CANDIDATE) {
			_gui->LogError("Error %d while selecting target release\n", result.base);
			return;
		}

		CandidateVersion const& candidate = std::get<CandidateVersion>(result.detail);
		if (!_launcherUpdateManager.DoUpdate(Launcher::version, candidate.version.c_str(), candidate.url.c_str())) {
			return;
		}

		FinishSelfUpdate();
	}

	void InstallationManager::FinishSelfUpdate() {
		Updater::Finalizer finalizer;
		Updater::FinalizationResult result = finalizer.Finalize();

		if (!std::holds_alternative<Updater::FinalizationCommunicationResult>(result)) {
			HandleFinalizationResult(result);
			return;
		}

		Updater::FinalizationCommunicationResult commResult = std::get<Updater::FinalizationCommunicationResult>(result);
		if (commResult != Updater::FINALIZATION_COMM_INTERNAL_OK && 
			commResult != Updater::FINALIZATION_COMM_INFO_TIMEOUT) {
			HandleFinalizationResult(result);
			return;
		}

		do {
			commResult = finalizer.ResumeFinalize();
		} while (commResult == Updater::FINALIZATION_COMM_INTERNAL_OK || commResult == Updater::FINALIZATION_COMM_INFO_TIMEOUT);

		HandleFinalizationResult(commResult);
	}
}