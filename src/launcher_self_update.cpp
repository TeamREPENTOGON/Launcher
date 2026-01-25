#include <string>
#include <filesystem>

#include <wx/progdlg.h>

#include "launcher/app.h"
#include "launcher/launcher_self_update.h"
#include "launcher/version.h"
#include "launcher/steam_workshop.h"
#include "shared/github_executor.h"
#include "shared/logger.h"
#include "shared/launcher_update_checker.h"
#include "wx/busyinfo.h"

namespace Launcher {
	void HandleSelfUpdate(LauncherMainWindow* mainWindow, bool allowUnstable, bool force) {
		Shared::LauncherUpdateChecker checker;
		std::string version, url;
		curl::DownloadStringResult result;
		Shared::SteamLauncherUpdateStatus steamUpdateStatus;
		bool updateAvailable = false;

		// Lil scope for the wxBusyInfo
		{
			wxBusyInfo wait("Checking for launcher updates, please wait...", mainWindow);

			updateAvailable = checker.IsSelfUpdateAvailable(allowUnstable, force, version, url, result, steamUpdateStatus);

			if (!updateAvailable && steamUpdateStatus == Shared::STEAM_LAUNCHER_UPDATE_FAILED) {
				// (Re)generate steamentrydir.txt (also checks the workshop item being subbed/downloaded) and try again.
				if (!SteamWorkshop::CreateSteamEntryDirFile()) {
					Logger::Error("Failed to create steamentrydir.txt\n");
				}
				updateAvailable = checker.IsSelfUpdateAvailable(allowUnstable, force, version, url, result, steamUpdateStatus);
			}
		}

		if (updateAvailable) {
			mainWindow->Show();

			std::ostringstream stream;
			stream << "A new version of the launcher is available (" <<
				Launcher::LAUNCHER_VERSION << " -> " << version << ").\n" <<
				"Do you want to update the launcher ?";
			int msgResult = wxMessageBox(stream.str(), "New launcher release available",
				wxYES_NO | wxICON_INFORMATION, mainWindow);

			if (msgResult == wxYES || msgResult == wxOK) {
				wxGetApp().RestartForSelfUpdate(url, version);
			}
		} else if (result != curl::DOWNLOAD_STRING_OK || steamUpdateStatus == Shared::STEAM_LAUNCHER_UPDATE_FAILED) {
			wxMessageBox("An error was encountered while checking for launcher updates. Check the log file for more details.", "REPENTOGON Launcher Error", wxICON_ERROR, mainWindow);
		}
	}
}
