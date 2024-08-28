#pragma once

#include <optional>
#include <string>
#include <tuple>

#include "launcher/filesystem.h"
#include "launcher/updater.h"
#include "launcher/self_update.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	class InstallationManager {
	public:
		enum SelfUpdateResult {
			/* Updater is up-to-date. */
			SELF_UPDATE_UTD,
			/* Error occured while attempting self update. */
			SELF_UPDATE_ERR,
			/* Self update started, but launcher timedout waiting for updater. */
			SELF_UPDATE_PARTIAL
		};

		enum CheckSelfUpdateResultCode {
			SELF_UPDATE_CHECK_UTD,
			SELF_UPDATE_CHECK_NEW,
			SELF_UPDATE_CHECK_ERR
		};

		struct CheckSelfUpdateResult {
			CheckSelfUpdateResultCode code;
			std::string url;
			std::string version;
		};

		InstallationManager(ILoggableGUI* gui);
		SelfUpdateResult HandleSelfUpdateResult(SelfUpdateErrorCode const& result);
		/* Return true if attempts should continue, false otherwise. */
		bool ResumeSelfUpdate(int currentRetry, int maxRetries);

		/* Initialize the folder in the filesystem structure.
		 * 
		 * *needIsaacFolder is set to false if the launcher is able to open and
		 * read the general configuration file, and finds the path to isaac-ng.exe
		 * in it.
		 * 
		 * *canWriteConfiguration is set to true as soon as the launcher is able
		 * to open the configuration file.
		 */
		void InitFolders(bool* needIsaacFolder, bool* canWriteConfiguration);
		std::optional<std::string> ConfiguraIsaacPath();
		bool CheckIsaacInstallation();
		CheckSelfUpdateResult CheckSelfUpdates(bool allowPreReleases);
		bool CheckIsaacVersion();
		bool UninstallLegacyRepentogon();
		bool RemoveFile(const char* file);

		enum RepentogonInstallationCheckResult {
			REPENTOGON_INSTALLATION_CHECK_OK,
			REPENTOGON_INSTALLATION_CHECK_LEGACY,
			REPENTOGON_INSTALLATION_CHECK_KO,
			// Installation is legacy, and launcher could not uninstall it
			REPENTOGON_INSTALLATION_CHECK_KO_LEGACY,
			REPENTOGON_INSTALLATION_CHECK_NONE
		};

		RepentogonInstallationCheckResult CheckRepentogonInstallation(bool isRetry, bool isUpdate);

		/* Manage a legacy installation of Repentogon.
		 *
		 * If a legacy installation of Repentogon is found, prompt the user for
		 * its removal. If the user agrees, uninstall the old version.
		 *
		 * This function indicates whether the user chose to keep the old
		 * installation, or if they chose to remove it, and if so what was the
		 * result of the removal.
		 */
		RepentogonInstallationCheckResult ManageLegacyInstallation();

		/* Display information about why the current Repentogon installation is
		 * broken.
		 */
		void DebugDumpBrokenRepentogonInstallation();
		void DisplayRepentogonFilesVersion(int tabs, bool isUpdate);

		bool InstallRepentogon(rapidjson::Document& document);

		enum DownloadInstallRepentogonResult {
			DOWNLOAD_INSTALL_REPENTOGON_OK,
			DOWNLOAD_INSTALL_REPENTOGON_UTD,
			DOWNLOAD_INSTALL_REPENTOGON_ERR
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
		 *
		 * Return false if the request for up-to-dateness fails in any way.
		 * Return true if there is an update available.
		 * Return false if the request succeeds, and the installation is up-to-date
		 * and force is false.
		 * Return true if the request succeeds, and the installation is up-to-date
		 * and force is true.
		 */
		bool CheckRepentogonUpdates(rapidjson::Document& document, bool allowPreReleases, bool force);

		inline RepentogonInstallationCheckResult GetRepentogonInstallationCheck() const {
			return _repentogonInstallationState;
		}

		inline bool IsValidRepentogonInstallation(bool allowLegacy) const {
			return _repentogonInstallationState == REPENTOGON_INSTALLATION_CHECK_OK ||
				(allowLegacy && IsLegacyRepentogonInstallation());
		}

		inline bool IsLegacyRepentogonInstallation() const {
			return _repentogonInstallationState == REPENTOGON_INSTALLATION_CHECK_LEGACY;
		}

	public:
		fs::Installation _installation;
		Updater _updater;
		SelfUpdater _selfUpdater;

	private:
		ILoggableGUI* _gui;
		std::string _zhlVersion;
		std::string _repentogonVersion;
		RepentogonInstallationCheckResult _repentogonInstallationState = REPENTOGON_INSTALLATION_CHECK_NONE;
	};

}