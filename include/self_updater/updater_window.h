#pragma once

#include "wx/wx.h"

#include "self_updater/updater.h"
#include "self_updater/synchronization.h"

namespace Updater {
	class App : public wxApp {
	public:
		bool OnInit() override;

	private:
		void ParseArgs() const;
	};

	class Updater : public wxFrame {
	public:
		static constexpr const size_t BUFF_SIZE = 4096;

		Updater();

		bool DoUpdate();
		bool ProcessSynchronizationResult(Synchronization::SynchronizationResult result);

	private:
		wxTextCtrl* _logWindow;
		bool _logError = false;
		static char _printBuffer[BUFF_SIZE];
		UpdaterBackend _updater;

		void Log(const char* fmt, ...);
		void LogError(const char* fmt, ...);

		void Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, ...);
		void Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, va_list va);

		void LogGithubDownloadAsString(const char* prefix, Github::DownloadAsStringResult code);

		bool DoPreUpdateChecks();
		bool DownloadUpdate(LauncherUpdateData* data);
		bool PostDownloadChecks(bool downloadOk, LauncherUpdateData* data);
	};
}