#pragma once

#include "wx/wx.h"

#include "launcher/self_updater/launcher_updater.h"
#include "shared/loggable_gui.h"
#include "unpacker/synchronization.h"

namespace Updater {
	class LauncherUpdateManager {
	public:
		static constexpr const size_t BUFF_SIZE = 4096;

		LauncherUpdateManager(ILoggableGUI* gui);

		bool DoUpdate(const char* from, const char* to, const char* url);
		bool ProcessSynchronizationResult(Synchronization::SynchronizationResult result);

	private:
		ILoggableGUI* _gui;
		bool _logError = false;
		static char _printBuffer[BUFF_SIZE];
		LauncherUpdater _updater;

		void LogGithubDownloadAsString(const char* prefix, Github::DownloadAsStringResult code);

		bool DoPreUpdateChecks();
		bool DownloadUpdate(LauncherUpdateData* data);
		bool PostDownloadChecks(bool downloadOk, LauncherUpdateData* data);
		bool ExtractArchive(LauncherUpdateData* data);
	};
}