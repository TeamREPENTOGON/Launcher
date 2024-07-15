#pragma once

#include "wx/wx.h"

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

		void DoUpdate();

	private:
		wxTextCtrl* _logWindow;
		bool _logError = false;
		static char _printBuffer[BUFF_SIZE];

		void Log(const char* fmt, ...);
		void LogError(const char* fmt, ...);

		void Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, ...);
		void Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, va_list va);
	};
}