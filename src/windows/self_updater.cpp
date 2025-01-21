#include <sstream>

#include "launcher/version.h"
#include "launcher/windows/self_updater.h"
#include "launcher/self_updater/launcher_update_manager.h"

#include "wx/wx.h"

SelfUpdaterWindow::SelfUpdaterWindow() : wxFrame(NULL, wxID_ANY, "REPENTOGON Launcher Self-Updater"),
	_logWindow(new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(-1, -1),
		wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH)), _updateManager(&_logWindow) {
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
	using lu = Updater::LauncherUpdateManager;
	bool result = false;

	std::string version;
	std::string url;

	lu::SelfUpdateCheckResult checkResult = _updateManager.CheckSelfUpdateAvailability(false, version, url);
	switch (checkResult) {
	case lu::SELF_UPDATE_CHECK_UP_TO_DATE:
		_logWindow.Log("Launcher is up-to-date\n");
		result = true;
		break;

	case lu::SELF_UPDATE_CHECK_ERR_GENERIC: {
		wxMessageDialog dialog(this, "An error was encountered while checking for the availability of updates. Check the log file for more details.",
			"Error", wxOK | wxICON_ERROR);
		dialog.ShowModal();
		result = false;
		break;
	}

	case lu::SELF_UPDATE_CHECK_UPDATE_AVAILABLE:
		if (PromptSelfUpdate(version, url)) {
			result = DoSelfUpdate(version, url);
			if (!result) {
				wxMessageDialog dialog(this, "An error was encountered while trying to update the launcher. Check the log file for more details.",
					"Error", wxOK | wxICON_ERROR);
				dialog.ShowModal();
			}
		} else {
			_logWindow.Log("Launcher updated rejected by user\n");
			result = true;
		}
		break;

	default: {
		std::ostringstream msg;
		msg << "Unknown error code " << checkResult << " received when checking for availability of self updates, please report this a bug.";
		wxMessageDialog dialog(this, msg.str(), "Error", wxOK | wxICON_ERROR);
		dialog.ShowModal();
		result = false;
		break;
	}
	}

	return result;
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

bool SelfUpdaterWindow::DoSelfUpdate(std::string const& version, std::string const& url) {
	return _updateManager.DoUpdate(Launcher::version, version.c_str(), url.c_str());
}