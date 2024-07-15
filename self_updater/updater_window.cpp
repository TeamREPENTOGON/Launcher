#include <WinSock2.h>

#include "wx/cmdline.h"

#include "shared/externals.h"
#include "self_updater/updater_window.h"

namespace Externals {
	void Init();
}

namespace Updater {
	struct CLIParams {
		wxString to, from;
		wxString lockFilePath;
	};

	static CLIParams params;

	char Updater::_printBuffer[Updater::BUFF_SIZE] = { 0 };

	bool App::OnInit() {
		Externals::Init();

		ParseArgs();

		Updater* frame = new Updater();
		frame->Show();
		frame->DoUpdate();
		return true;
	}

	void App::ParseArgs() const {
		wxCmdLineParser parser;
		parser.SetCmdLine(GetCommandLineW());
		parser.AddOption("f", "from", "Version from which the upgrade is being performed");
		parser.AddOption("t", "to", "Version to which the upgrade is being performed");
		parser.AddLongOption("lock-file", "Path to the lock file used to synchronize with the launcher");
		parser.Parse(false);
		
		if (!parser.Found("f", &params.from)) {
			params.from = "unknown";
		}
		
		if (!parser.Found("t", &params.to)) {
			params.to = "unknown";
		}

		if (!parser.Found("lock-file", &params.lockFilePath)) {
			wxMessageDialog dialog(NULL, "No lock file specified, please run from the launcher", "Fatal error");
			dialog.ShowModal();
			exit(-1);
		}
	}

	Updater::Updater() : wxFrame(NULL, -1, "REPENTOGON Launcher Updater") {
		SetSize(1024, 650);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(sizer);
		_logWindow = new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1), wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
		sizer->Add(_logWindow, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM | wxRIGHT, 20);
		SetBackgroundColour(wxColour(237, 237, 237));
	}

	void Updater::DoUpdate() {
		Log("Updating the REPENTOGON launcher\n");
		Log("Update scheduled from version %s to version %s\n", params.from.c_str().AsChar(), params.to.c_str().AsChar());
		Log("Using lock file %s\n", params.lockFilePath.c_str().AsChar());
		LogError("Test error\n");
	}

	void Updater::Log(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log("[INFO]", false, wxTextAttr(), fmt, va);
		va_end(va);
	}

	void Updater::LogError(const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		wxTextAttr attr(*wxRED);
		Log("[ERROR]", false, attr, fmt, va);
		va_end(va);
	}

	void Updater::Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, ...) {
		va_list va;
		va_start(va, fmt);
		Log(prefix, nl, attr, fmt, va);
		va_end(va);
	}

	void Updater::Log(const char* prefix, bool nl, wxTextAttr const& attr, const char* fmt, va_list va) {
		wxString string;
		string.append(prefix);
		string.append(" ");

		int n = vsnprintf(NULL, 0, fmt, va);

		if (n < 0) {
			if (_logError) {
				MessageBoxA(NULL, "Unable to log text", "Internal fatal error", MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND | MB_OK);
				exit(-1);
			}

			_logError = true;
			Log("[ERROR]", false, "Low-level error when formatting log text\n");
			_logError = false;
			return;
		}

		char* buffer = _printBuffer;
		if (n >= 4096) {
			buffer = (char*)malloc(n + 1);
			if (!buffer) {
				_logError = true;
				Log("[ERROR]", false, "Unable to allocate memory to write log\n");
				_logError = false;
				return;
			}
		}

		vsnprintf(buffer, n + 1, fmt, va);

		string.append(buffer);
		if (nl && buffer[n] != '\n')
			string.append("\n");

		wxTextAttr original = _logWindow->GetDefaultStyle();
		_logWindow->SetDefaultStyle(attr);
		_logWindow->AppendText(string);
		_logWindow->SetDefaultStyle(original);

		if (n >= 4096)
			free(buffer);
	}
}

wxIMPLEMENT_APP(Updater::App);