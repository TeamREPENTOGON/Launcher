#include <utility>

#include <wx/dialog.h>
#include <wx/sizer.h>

#include "launcher/cli.h"
#include "launcher/windows/launcher.h"
#include "launcher/windows/repentogon_installer.h"
#include "launcher/log_helpers.h"
#include "shared/logger.h"

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
		_mainFrame->PreInit();
		_mainFrame->SetFocus();
		_mainFrame->EnableInterface(true);
		// _mainFrame->Show();
	}

	if (!sCLI->RepentogonInstallerWait() && !_synchronous) {
		Destroy();
	}
}