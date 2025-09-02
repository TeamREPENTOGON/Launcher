#include <string>

#include <wx/progdlg.h>

#include "launcher/launcher_self_update.h"
#include "launcher/version.h"
#include "shared/github_executor.h"
#include "shared/logger.h"
#include "shared/launcher_update_checker.h"

namespace Launcher {
	static const char* SelfUpdaterExePath = "./REPENTOGONLauncherUpdater.exe";

	enum SelfUpdateResult {
		SELF_UPDATE_ERR_GENERIC,
		SELF_UPDATE_ERR_EXE
	};

	static SelfUpdateResult DoSelfUpdate(const char* version, const char* url);

	void HandleSelfUpdate(LauncherMainWindow* mainWindow, bool allowUnstable, bool force) {
		wxProgressDialog dialog("Launcher updater checker",
			"Checking for updates of the launcher...", 100, mainWindow,
			wxPD_APP_MODAL | wxPD_CAN_ABORT);

		Shared::LauncherUpdateChecker checker;
		std::string version, url;
		curl::DownloadStringResult result;
		if (checker.IsSelfUpdateAvailable(allowUnstable, force, version, url, &result)) {
			mainWindow->Show();

			std::ostringstream stream;
			stream << "A new version of the launcher is available (version " <<
				version << ", from version " << Launcher::version << ").\n" <<
				"Do you want to update the launcher ?";
			int msgResult = wxMessageBox(stream.str(), "New launcher release available",
				wxYES_NO, &dialog);

			if (msgResult == wxYES || msgResult == wxOK) {
				SelfUpdateResult updateResult = DoSelfUpdate(version.c_str(),
					url.c_str());

				std::ostringstream errMsg;
				errMsg << "Error while attempting self-update: check the ";
				switch (updateResult) {
				case SELF_UPDATE_ERR_GENERIC:
					errMsg << "launcher.log";
					break;

				case SELF_UPDATE_ERR_EXE:
					errMsg << "updater.log";
					break;

				default:
					errMsg << "[[internal error " << (int)updateResult << " please "
						"report this to receive a place in the Repentogon debug "
						"team hall of fame]]";
					break;
				}

				errMsg << " file for details";

				wxMessageBox(errMsg.str(), "Error during self update", wxOK, &dialog);
			}
		}
	}

	SelfUpdateResult DoSelfUpdate(const char* version, const char* url) {
		Logger::Info("Launching self-updater to check for launcher updates...\n");

		char cli[512];
		int len = snprintf(cli, sizeof(cli), "%s --launcherpid %d --version %s --url %s",
			SelfUpdaterExePath, GetCurrentProcessId(), version, url);
		if (len < 0 || len >= sizeof(cli)) {
			Logger::Error("Failed to start self-updater process: command-line "
				"arguments exceeded buffer size %d\n", sizeof(cli));
			return SELF_UPDATE_ERR_GENERIC;
		}

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, cli, NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
		if (!ok) {
			Logger::Error("Failed to start self-updater process (error code %d)\n", GetLastError());
			return SELF_UPDATE_ERR_GENERIC;
		}

		// Wait for the updater process to close.
		// If an update is initiated, the updater will terminate us.
		// The updater is responsible for having visible UI at this time.
		WaitForSingleObject(info.hProcess, INFINITE);

		// The updater closed and we were not terminated.
		// Assume that this means either there was no update, or the user rejected the update.
		CloseHandle(info.hProcess);
		CloseHandle(info.hThread);

		return SELF_UPDATE_ERR_EXE;
	}
}