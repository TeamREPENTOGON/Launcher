#include <string>

#include <wx/progdlg.h>

#include "launcher/launcher_self_update.h"
#include "launcher/version.h"
#include "shared/github_executor.h"
#include "shared/logger.h"
#include "shared/launcher_update_checker.h"

namespace Launcher {
	static const char* SelfUpdaterExePath = "../REPENTOGONLauncher.exe";

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
			stream << "A new version of the launcher is available (" <<
				Launcher::LAUNCHER_VERSION << " -> " << version << ").\n" <<
				"Do you want to update the launcher ?";
			int msgResult = wxMessageBox(stream.str(), "New launcher release available",
				wxYES_NO, &dialog);

			if (msgResult == wxYES || msgResult == wxOK) {
				SelfUpdateResult updateResult = DoSelfUpdate(version.c_str(), url.c_str());

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

	std::string BuildUpdaterCli(const char* version, const char* url) {
		std::ostringstream cli;
		cli << SelfUpdaterExePath << " --launcherpid " << GetCurrentProcessId() << " --version " << version << " --url " << url;
		for (int i = 1; i < wxTheApp->argc; ++i) {
			cli << " " << wxTheApp->argv[i];
		}
		return cli.str();
	}

	SelfUpdateResult DoSelfUpdate(const char* version, const char* url) {
		std::string cli = BuildUpdaterCli(version, url);

		Logger::Info("Launching self-updater: %s\n", cli.c_str());

		PROCESS_INFORMATION info;
		memset(&info, 0, sizeof(info));

		STARTUPINFOA startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));

		BOOL ok = CreateProcessA(SelfUpdaterExePath, (LPSTR)cli.c_str(), NULL, NULL, false, 0, NULL, NULL, &startupInfo, &info);
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