// #include <Windows.h>

#include "self_updater/updater.h"
#include "self_updater/updater_resources.h"

#include "launcher/self_update.h"
#include "rapidjson/document.h"
#include "shared/github.h"

namespace Launcher {
	[[noreturn]] static void SelfUpdate(std::string const& releaseUrl);
	static SelfUpdateResult ExtractUpdater();
	static Github::FetchUpdatesResult FetchReleases(rapidjson::Document& answer);
	static bool CheckSelfUpdateAvailable(rapidjson::Document const& releases);

	const char* SelfUpdaterExePath = "./repentogon_launcher_self_updater.exe";

	SelfUpdateResult ExtractUpdater() {
		HRSRC updater = FindResource(NULL, MAKEINTRESOURCE(IDB_EMBEDEXE1), RT_RCDATA);
		if (!updater) {
			return SELF_UPDATE_RESOURCE_NOT_FOUND;
		}

		HGLOBAL global = LoadResource(NULL, updater);
		if (!global) {
			return SELF_UPDATE_LOAD_FAILED;
		}

		DWORD size = SizeofResource(NULL, updater);
		if (size == 0) {
			return SELF_UPDATE_INVALID_SIZE;
		}

		void* data = LockResource(global);
		if (!data) {
			return SELF_UPDATE_LOCK_FAILED;
		}

		const char* filename = SelfUpdaterExePath;
		FILE* output = fopen(filename, "wb");
		if (!output) {
			return SELF_UPDATE_CANNOT_OPEN_TEMPORARY_FILE;
		}

		size_t count = fwrite(data, size, 1, output);
		if (count != size) {
			// LogWarn("Inconsistent amount of data written: expected %d, wrote %lu", size, count);
		}

		fclose(output);
	}

	SelfUpdateResult DoSelfUpdate() {
		SelfUpdateResult extractResult = ExtractUpdater();
		
		std::string updateStatePath = "repentogon_launcher_self_updater_state";
		FILE* updateState = fopen(updateStatePath.c_str(), "wb");
		if (!updateState) {
			return SELF_UPDATE_CANNOT_OPEN_LOCK_FILE;
		}

		fprintf(updateState, "%d", ::Updater::UpdateState::UPDATE_STATE_INIT);
		fflush(updateState);
		fclose(updateState);

		char cli[4096] = { 0 };
		int printfCount = snprintf(cli, 4096, "--lock-file=\"%s\"", updateStatePath.c_str());
		if (printfCount < 0) {
			return SELF_UPDATE_NO_CLI;
		}

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, cli, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			return SELF_UPDATE_CREATE_PROCESS_FAILED;
		}

		HANDLE child = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, false, info.dwProcessId);

		// LogNoNL("Waiting until self updater is ready... ");
		WaitForInputIdle(child, INFINITE);
		// Log("Done");

		ExitProcess(0);
	}
}