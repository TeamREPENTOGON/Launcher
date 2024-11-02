#pragma once

#include "launcher/self_updater/launcher_updater.h"
#include "launcher/self_updater/finalizer.h"
#include "launcher/self_update.h"
#include "shared/loggable_gui.h"
#include "unpacker/synchronization.h"

namespace Updater {
	class LauncherUpdateManager {
	public:
		enum SelfUpdateCheckResult {
			SELF_UPDATE_CHECK_UP_TO_DATE,
			SELF_UPDATE_CHECK_UPDATE_AVAILABLE,
			SELF_UPDATE_CHECK_ERR_GENERIC
		};
	public:
		static constexpr const size_t BUFF_SIZE = 4096;

		LauncherUpdateManager(ILoggableGUI* gui);

		SelfUpdateCheckResult CheckSelfUpdateAvailability(bool allowPreRelease, std::string& version, std::string& url);
		void ForceSelfUpdate(bool allowPreReleases);
		bool DoUpdate(const char* from, const char* to, const char* url);
		bool ProcessSynchronizationResult(Synchronization::SynchronizationResult result);

	private:
		ILoggableGUI* _gui;
		bool _logError = false;
		static char _printBuffer[BUFF_SIZE];
		LauncherUpdater _updater;
		Launcher::SelfUpdater _selfUpdater;

		void LogGithubDownloadAsString(const char* prefix, Github::DownloadAsStringResult code);

		bool DoPreUpdateChecks();
		bool DownloadUpdate(LauncherUpdateData* data);
		bool PostDownloadChecks(bool downloadOk, LauncherUpdateData* data);
		bool ExtractArchive(LauncherUpdateData* data);

		void FinalizeUpdate();

		void HandleFinalizationResult(Updater::FinalizationResult const& result);
	};
}