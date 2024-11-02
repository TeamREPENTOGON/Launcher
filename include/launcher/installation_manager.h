#pragma once

#include <optional>
#include <string>
#include <tuple>

#include "launcher/installation.h"
#include "launcher/repentogon_updater.h"
#include "launcher/self_update.h"
#include "launcher/self_updater/launcher_update_manager.h"
#include "launcher/self_updater/finalizer.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	class InstallationManager {
	public:
		InstallationManager(ILoggableGUI* gui, Installation* installation);

		/* Install a specific release of Repentogon. */
		bool InstallRepentogon(rapidjson::Document& document);

		enum DownloadInstallRepentogonResult {
			DOWNLOAD_INSTALL_REPENTOGON_OK,
			DOWNLOAD_INSTALL_REPENTOGON_UTD,
			DOWNLOAD_INSTALL_REPENTOGON_ERR,
			DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES
		};

		enum CheckRepentogonUpdatesResult {
			CHECK_REPENTOGON_UPDATES_UTD,
			CHECK_REPENTOGON_UPDATES_NEW,
			CHECK_REPENTOGON_UPDATES_ERR
		};

		/* Download and install the latest release of Repentogon.
		 *
		 * Parameter force indicates whether the function will download and
		 * install the latest release even if the installation is up-to-date.
		 *
		 * If a Repentogon installation has been detected, the function will
		 * check for the presence of dsound.dll, asking the user if they want
		 * that file deleted should it be found.
		 */
		DownloadInstallRepentogonResult InstallLatestRepentogon(bool force, bool allowPreReleases);

		/* Check if an update is available.
		 *
		 * Parameter force will cause the function to consider that an update is
		 * available even if the installation is up-to-date.
		 */
		CheckRepentogonUpdatesResult CheckRepentogonUpdates(rapidjson::Document& document, bool allowPreReleases, bool force);

		/* Display information about why the current Repentogon installation is
		 * broken.
		 */
		void DebugDumpBrokenRepentogonInstallation();
		void DisplayRepentogonFilesVersion(int tabs, bool isUpdate);

	private:
		void DebugDumpBrokenRepentogonInstallationDLL(const char* context, const char* libname, LoadableDlls dll,
			std::string const& (Installation::* ptr)() const, bool* found);
		bool RemoveFile(const char* file);
		bool HandleLegacyInstallation();

		Installation* _installation;
		ILoggableGUI* _gui;

		/* Prior to update, for logging purpose. */
		std::string _zhlVersion;
		std::string _repentogonVersion;
		std::string _loaderVersion;
	};

}