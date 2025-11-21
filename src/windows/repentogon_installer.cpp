#include <utility>

#include <wx/dialog.h>
#include <wx/sizer.h>

#include "launcher/cli.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/repentogon_installer.h"
#include "launcher/log_helpers.h"
#include "shared/logger.h"
#include <filesystem>

wxBEGIN_EVENT_TABLE(RepentogonInstallerFrame, wxFrame)
EVT_CLOSE(RepentogonInstallerFrame::OnClose)
wxEND_EVENT_TABLE()

RepentogonInstallerFrame::RepentogonInstallerFrame(wxWindow* parent,
	bool synchronous, Launcher::Installation* installation, bool forceUpdate,
	bool allowUnstable) : wxFrame(parent, wxID_ANY, "Repentogon updater"),
		_synchronous(synchronous), _installation(installation),
		_forceUpdate(forceUpdate), _allowUnstable(allowUnstable) {

}

RepentogonInstallerFrame::~RepentogonInstallerFrame() {
	if (_mainFrame) {
		_mainFrame->SetFocus();
	}
}

void RepentogonInstallerFrame::Initialize() {
	wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	wxTextCtrl* logWindow = new wxTextCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxDefaultSize,
		wxTE_READONLY | wxTE_MULTILINE | wxTE_RICH);
	logWindow->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
		wxTextCtrl* ctrl = static_cast<wxTextCtrl*>(event.GetEventObject());
			if (((ctrl->GetNumberOfLines() > 2) || (ctrl->GetForegroundColour() == *wxRED)) && (!this->IsShown())) { //this is so the window doesnt just popup to tell me that there were no updates, which is pretty jarrying
				this->Show();
				this->SetFocus();
			}
		});
	SetSize(640, 480);
	SetBackgroundColour(wxColour(240, 240, 240));
	SetSizeHints(640, 480, 640, 480);
	SetSizer(sizer);
	sizer->Add(logWindow, wxSizerFlags().Expand().Border(240, 5));
	logWindow->SetSizeHints(640, 480, 640, 480);
	CentreOnScreen();

	_logWindow = logWindow;
}

void RepentogonInstallerFrame::OnClose(wxCloseEvent& event) {
	std::unique_lock<std::mutex> lck(_installerMutex);
	if (_helper) {
		switch (_helper->Terminate()) {
		case RepentogonInstallerHelper::TERMINATE_CANCEL_REJECTED:
			if (event.CanVeto()) {
				event.Veto();
				return;
			}
			break;

		case RepentogonInstallerHelper::TERMINATE_OK:
			Destroy();
			break;

		default:
			break;
		}
	}
}

void RepentogonInstallerFrame::InstallRepentogon() {
	std::unique_lock<std::mutex> lck(_installerMutex);
	_wasinstalled = std::filesystem::exists(_installation->GetIsaacInstallation().GetMainInstallation().GetFolderPath() + "\\Repentogon");
	_helper = std::make_unique<RepentogonInstallerHelper>(this, _installation,
		_allowUnstable, _forceUpdate, _logWindow);
	_helper->Install(std::bind_front(&RepentogonInstallerFrame::OnRepentogonInstalled, this));

	if (_synchronous) {
		_helper->Wait();
	}
}

void RepentogonInstallerFrame::OnRepentogonInstalled(bool completed,
	Launcher::RepentogonInstaller::DownloadInstallRepentogonResult result) {
	_installerCompleted = completed;
	_downloadInstallResult = result;

	if (_mainFrame) {
		_mainFrame->CallAfter(&Launcher::LauncherMainWindow::Init);
	}

	if (!_wasinstalled && (result == Launcher::RepentogonInstaller::DOWNLOAD_INSTALL_REPENTOGON_OK))
	{
		_wasinstalled = true;
		int res = wxMessageBox(
			"REPENTOGON has successfully installed!\nTake in mind that it runs on an older version of REP+, which means it may miss some features. But remember you can still launch in Vanilla mode on the latest version if needed, both versions exist simultanously! ",
			"Sucessfully installed REPENTOGON",
			wxOK | wxOK_DEFAULT | wxICON_INFORMATION,
			this
		);
	}

	if (!sCLI->RepentogonInstallerWait() && !_synchronous) {
		Destroy();
	}
}