#include <string>

#include "shared/logger.h"
#include "shared/launcher_update_checker.h"
#include "launcher/launcher_self_update.h"

namespace Launcher {
	static const char* SelfUpdaterExePath = "./REPENTOGONLauncherUpdater.exe";

	bool CheckForSelfUpdate(const bool allowUnstable) {
		Logger::Info("Launching self-updater to check for launcher updates...\n");

		char cli[256];
		sprintf(cli, "%s%s --launcherpid %d", SelfUpdaterExePath, allowUnstable ? " --unstable" : "", GetCurrentProcessId());

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, cli, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			Logger::Info("Failed to start self-updater process (error code %d)\n", GetLastError());
			return false;
		}

		// Wait for the updater process to close.
		// If an update is initiated, the updater will terminate us.
		// The updater is responsible for having visible UI at this time.
		WaitForSingleObject(info.hProcess, INFINITE);

		// The updater closed and we were not terminated.
		// Assume that this means either there was no update, or the user rejected the update.
		CloseHandle(info.hProcess);
		CloseHandle(info.hThread);

		Logger::Info("No self-update initiated.\n");

		return true;
	}
}