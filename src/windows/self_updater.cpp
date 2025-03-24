#include <sstream>

#include "launcher/version.h"
#include "launcher/launcher_self_update.h"
#include "launcher/windows/self_updater.h"
#include "shared/launcher_update_checker.h"

#include "wx/wx.h"

SelfUpdaterWindow::SelfUpdaterWindow() : wxFrame(NULL, wxID_ANY, "REPENTOGON Launcher Self-Updater"),
	_logWindow(new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1),
		wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH)) {
	Initialize();
}

void SelfUpdaterWindow::Initialize() {
	const char* text = "Checking for launcher's updates...";
	_mainText = new wxTextCtrl(this, -1, text);

	wxSizerFlags flags;
	flags.Expand();

	wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(_mainText, flags);
	sizer->Add(_logWindow.Get(), flags);
	
	SetSizerAndFit(sizer);
}

bool SelfUpdaterWindow::HandleSelfUpdate() {
	std::string version;
	std::string url;

	_logWindow.Log("Checking for availability of launcher updates...");
	Github::DownloadAsStringResult downloadReleasesResult;
	if (Shared::LauncherUpdateChecker().IsSelfUpdateAvailable(/*allowPreReleases=*/false, false, version, url, &downloadReleasesResult)) {
		_logWindow.Log("New version of the launcher available: %s (can be downloaded from %s)\n", version.c_str(), url.c_str());
		_logWindow.Log("Prompting user to ask if they want to update...");
		if (PromptSelfUpdate(version, url)) {
			_logWindow.Log("Update approved. Starting updater...");
			if (Launcher::StartUpdater(url)) {
				return true;
			} else {
				_logWindow.Log("Failed to start the updater");
				wxMessageDialog dialog(this, "An error was encountered while trying to update the launcher. Check the log file for more details.",
					"Error", wxOK | wxICON_ERROR);
				dialog.ShowModal();
			}
		} else {
			_logWindow.Log("Launcher updated rejected by user");
		}
	} else if (downloadReleasesResult != Github::DOWNLOAD_AS_STRING_OK) {
		std::ostringstream msg;
		msg << "Encountered error when checking for availability of self updates: " << Github::DownloadAsStringResultToLogString(downloadReleasesResult);
		_logWindow.Log(msg.str().c_str());
		msg << "\nCheck the log file for more details.";
		wxMessageDialog dialog(this, msg.str(), "Error", wxOK | wxICON_ERROR);
		dialog.ShowModal();
	} else {
		_logWindow.Log("Launcher is up to date");
	}

	return false;
}

bool SelfUpdaterWindow::PromptSelfUpdate(std::string const& version, std::string const& url) {
	std::ostringstream s;
	s << "An update is available for the launcher.\n" <<
		"It will update from version " << Launcher::version << " to version " << version << ".\n" <<
		"(You may also download manually from " << url << ").\n" <<
		"Do you want to update now ?";
	wxMessageDialog modal(this, s.str(), "Update Repentogon's Launcher ?", wxYES_NO);
	int result = modal.ShowModal();
	return result == wxID_YES || result == wxID_OK;
}