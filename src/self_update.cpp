// #include <Windows.h>

#include "self_updater/updater.h"
#include "self_updater/updater_resources.h"

#include "launcher/launcher.h"
#include "launcher/self_update.h"
#include "rapidjson/document.h"
#include "shared/github.h"
#include "shared/logger.h"

namespace Launcher {
	/* Struct used to resume a self update that timed out while waiting for the
	 * self updater to become ready.
	 */
	struct PartialSelfUpdate {
		/* Handle to the self updater. */
		HANDLE selfUpdaterHandle;
	};

	static std::optional<PartialSelfUpdate> partialUpdate;

	static SelfUpdateRunUpdaterResult RunUpdater(std::string const& url, std::string const& version);
	static SelfUpdateRunUpdaterResult RunUpdaterFinalize(HANDLE child);
	static SelfUpdateExtractionResult ExtractUpdater();
	static Github::DownloadAsStringResult FetchReleases(rapidjson::Document& answer);

	const char* ReleasesURL = "https://api.github.com/repos/TeamREPENTOGON/Launcher/releases";
	const char* SelfUpdaterExePath = "./repentogon_launcher_self_updater.exe";

	Github::DownloadAsStringResult FetchReleases(rapidjson::Document& answer) {
		Threading::Monitor<Github::GithubDownloadNotification> monitor;
		std::string releaseString;
		// return Github::FetchReleaseInfo(ReleasesURL, answer, &monitor);
		Github::DownloadAsStringResult result = Github::DownloadAsString(ReleasesURL, releaseString, nullptr);
		if (result != Github::DOWNLOAD_AS_STRING_OK) {
			return result;
		}

		answer.Parse(releaseString.c_str());
		if (answer.HasParseError()) {
			return Github::DOWNLOAD_AS_STRING_INVALID_JSON;
		}

		return Github::DOWNLOAD_AS_STRING_OK;
	}

	SelfUpdateExtractionResult ExtractUpdater() {
		HRSRC updater = FindResource(NULL, MAKEINTRESOURCE(IDB_EMBEDEXE1), RT_RCDATA);
		if (!updater) {
			return SELF_UPDATE_EXTRACTION_ERR_RESOURCE_NOT_FOUND;
		}

		HGLOBAL global = LoadResource(NULL, updater);
		if (!global) {
			return SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOAD_FAILED;
		}

		DWORD size = SizeofResource(NULL, updater);
		if (size == 0) {
			return SELF_UPDATE_EXTRACTION_ERR_BAD_RESOURCE_SIZE;
		}

		void* data = LockResource(global);
		if (!data) {
			return SELF_UPDATE_EXTRACTION_ERR_RESOURCE_LOCK_FAILED;
		}

		const char* filename = SelfUpdaterExePath;
		FILE* output = fopen(filename, "wb");
		if (!output) {
			return SELF_UPDATE_EXTRACTION_ERR_OPEN_TEMPORARY_FILE;
		}

		size_t count = fwrite(data, size, 1, output);
		if (count != 1) {
			fclose(output);
			return SELF_UPDATE_EXTRACTION_ERR_WRITTEN_SIZE;
		}

		fclose(output);
		return SELF_UPDATE_EXTRACTION_OK;
	}

	SelfUpdateRunUpdaterResult RunUpdater(std::string const& url, std::string const& version) {
		std::string updateStatePath = "repentogon_launcher_self_updater_state";
		FILE* updateState = fopen(updateStatePath.c_str(), "wb");
		if (!updateState) {
			return SELF_UPDATE_RUN_UPDATER_ERR_OPEN_LOCK_FILE;
		}

		fprintf(updateState, "%d", ::Updater::UpdateState::UPDATE_STATE_INIT);
		fflush(updateState);
		fclose(updateState);

		char cli[4096] = { 0 };
		int printfCount = snprintf(cli, 4096, "--lock-file=\"%s\" --from=\"%s\" --to=\"%s\" --url=\"%s\"", 
			updateStatePath.c_str(), Launcher::version, version.c_str(), url.c_str());
		if (printfCount < 0) {
			return SELF_UPDATE_RUN_UPDATER_ERR_GENERATE_CLI;
		}

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, cli, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			return SELF_UPDATE_RUN_UPDATER_ERR_CREATE_PROCESS;
		}

		HANDLE child = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, false, info.dwProcessId);
		if (child == INVALID_HANDLE_VALUE) {
			return SELF_UPDATE_RUN_UPDATER_ERR_OPEN_PROCESS;
		}

		return RunUpdaterFinalize(child);
	}

	SelfUpdateRunUpdaterResult RunUpdaterFinalize(HANDLE child) {
		// DWORD waitResult = WaitForInputIdle(child, 10000);
		DWORD waitResult = 0;
		switch (waitResult) {
		case 0:
			Logger::Info("RunUpdaterFinalize: WaitForInputIdle finished\n");
			ExitProcess(0);
			break;

		case WAIT_TIMEOUT:
			partialUpdate->selfUpdaterHandle = child;
			return SELF_UPDATE_RUN_UPDATER_ERR_WAIT_TIMEOUT;

		case WAIT_FAILED:
			Logger::Error("Launcher self-update: WaitForInputIdle returned WAIT_FAILED (GetLastError() = %d)\n", GetLastError());
			return SELF_UPDATE_RUN_UPDATER_ERR_WAIT;

		default:
			return SELF_UPDATE_RUN_UPDATER_ERR_WAIT;
		}
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool force, std::string& version, std::string& url);

	bool SelfUpdater::IsSelfUpdateAvailable(bool allowDrafts, bool force, 
		std::string& version, std::string& url, Github::DownloadAsStringResult* fetchReleasesResult) {
		Github::DownloadAsStringResult downloadResult = FetchReleases(_releasesInfo);
		if (fetchReleasesResult)
			*fetchReleasesResult = downloadResult;

		if (downloadResult != Github::DOWNLOAD_AS_STRING_OK) {
			return false;
		}

		rapidjson::Document& releases = _releasesInfo;
		_hasRelease = true;
		
		return SelectTargetRelease(releases, allowDrafts, force, version, url);
	}

	bool SelectTargetRelease(rapidjson::Document const& releases, bool allowPre,
		bool force, std::string& version, std::string& url) {
		auto releaseArray = releases.GetArray();
		for (auto const& release : releaseArray) {
			if (!release["prerelease"].IsTrue() || allowPre) {
				version = release["name"].GetString();
				url = release["url"].GetString();
				return true;
			}
		}

		return false;
	}

	SelfUpdateErrorCode SelfUpdater::DoSelfUpdate(bool allowPreRelease, bool force) {
		partialUpdate.reset();

		SelfUpdateErrorCode result;
		rapidjson::Document releases;

		Github::DownloadAsStringResult releasesResult = FetchReleases(releases);
		if (releasesResult != Github::DOWNLOAD_AS_STRING_OK) {
			result.base = SELF_UPDATE_UPDATE_CHECK_FAILED;
			result.detail.fetchUpdatesResult = releasesResult;
			return result;
		}

		std::string version, url;
		if (!SelectTargetRelease(releases, allowPreRelease, force, version, url)) {
			result.base = SELF_UPDATE_UP_TO_DATE;
			return result;
		}

		return DoSelfUpdate(version, url);
		
	}

	SelfUpdateErrorCode SelfUpdater::DoSelfUpdate(std::string const& version, std::string const& url) {
		partialUpdate.reset();

		SelfUpdateErrorCode result;
		SelfUpdateExtractionResult extractResult = ExtractUpdater();
		if (extractResult != SELF_UPDATE_EXTRACTION_OK) {
			result.base = SELF_UPDATE_EXTRACTION_FAILED;
			result.detail.extractionResult = extractResult;
			return result;
		}

		SelfUpdateRunUpdaterResult runResult = RunUpdater(url, version);
		result.base = SELF_UPDATE_SELF_UPDATE_FAILED;
		result.detail.runUpdateResult = runResult;

		return result;
	}

	SelfUpdateErrorCode SelfUpdater::ResumeSelfUpdate() {
		SelfUpdateErrorCode result;
		if (!partialUpdate) {
			result.base = SELF_UPDATE_SELF_UPDATE_FAILED;
			result.detail.runUpdateResult = SELF_UPDATE_RUN_UPDATER_ERR_INVALID_WAIT;
			return result;
		}


		SelfUpdateRunUpdaterResult runResult = RunUpdaterFinalize(partialUpdate->selfUpdaterHandle);
		/* Remember: if the above function returns, then the updater is not launched.
		 * If the updater is launched, the function never returns.
		 */

		result.base = SELF_UPDATE_SELF_UPDATE_FAILED;
		result.detail.runUpdateResult = runResult;

		return result;
	}
}