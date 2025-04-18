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

RepentogonInstallerFrame::RepentogonInstallerFrame(Launcher::Installation* installation,
	bool forceUpdate) :
		wxFrame(nullptr, wxID_ANY, "Repentogon updater"), _installation(installation),
		_forceUpdate(forceUpdate) {

}

RepentogonInstallerFrame::~RepentogonInstallerFrame() {
	wxASSERT(!_downloadThread.joinable());
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

bool RepentogonInstallerFrame::RequestCancel() {
	if (wxMessageBox("A Repentogon download is still in progress\nDo you really want to abort ?",
		"Please confirm", wxICON_QUESTION | wxYES_NO) != wxYES)
	{
		Logger::Info("User rejected cancel request");
		return false;
	}

	_cancelStart = std::chrono::steady_clock::now();
	_cancelRequested = 1;

	std::unique_lock<std::mutex> lck(_logWindowMutex);
	_logWindow->SetForegroundColour(*wxRED);
	wxFont font = _logWindow->GetFont();
	_logWindow->SetFont(font.Bold());
	_logWindow->AppendText("[WARNING] Cancelling download\n");
	_logWindow->SetFont(font);

	return true;
}

void RepentogonInstallerFrame::RequestReCancel() {
	Logger::Info("User requested cancellation of Repentogon download (%d)", _cancelRequested);
	++_cancelRequested;

	auto now = std::chrono::steady_clock::now();
	if (std::chrono::duration_cast<std::chrono::seconds>(now - _cancelStart).count() > 5) {
		bool expected = false;
		_terminateDownload.compare_exchange_strong(expected, true);
	}
}

void RepentogonInstallerFrame::OnClose(wxCloseEvent& event) {
	std::unique_lock<std::mutex> lock(_downloadMutex);
	if (_closing) {
		return;
	}

	if (_downloadInProgress) {
		if (!_cancelRequested) {
			if (!RequestCancel() && event.CanVeto()) {
				event.Veto();
				return;
			}
		} else {
			RequestReCancel();
		}

		return;
	}

	/* This acts as a safety to ensure the download thread will not join itself
	 * (which is the most likely scenario).
	 */
	if (_downloadThread.joinable() && std::this_thread::get_id() != _downloadThread.get_id()) {
		Logger::Info("RepentogonInstallerFrame: joining download thread\n");
		_downloadThread.join();
		Logger::Info("RepentogonInstallerFrame: joined download thread\n");
	} else {
		Logger::Warn("RepentogonInstallerFrame: detaching download thread\n");
		_downloadThread.detach();
	}

	if (_mainFrame) {
		_mainFrame->PreInit();
		_mainFrame->Show();
	}

	_closing = true;
	Destroy();
}

void RepentogonInstallerFrame::InstallRepentogon() {
	std::unique_lock<std::mutex> lock(_downloadMutex);

	if (_downloadInProgress) {
		Logger::Fatal("RepentogonInstallerFrame called while a download is already in progress\n");
		assert(false);
		// std::unreachable();
		__assume(0);
		return;
	}

	_downloadThread = std::thread(&RepentogonInstallerFrame::InstallRepentogonThread, this);
	_downloadInProgress = true;
}

void RepentogonInstallerFrame::InstallRepentogonThread() {
	Launcher::RepentogonInstaller installer(_installation);
	auto [future, monitor] = installer.InstallLatestRepentogon(
		_forceUpdate, _installation->GetLauncherConfiguration()->UnstableUpdates());
	bool shouldContinue = true;
	NotificationVisitor visitor(_logWindow, sCLI->RepentogonInstallerRefreshRate());
	while (shouldContinue && !_cancelRequested) {
		if (future.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
			shouldContinue = false;
		}

		while (std::optional<Launcher::RepentogonInstallationNotification> notification = monitor->Get()) {
			// Sub-efficient, but less likely to cause a cancellation request to be missed
			std::unique_lock<std::mutex> lck(_logWindowMutex);
			std::visit(visitor, notification->_data);
		}
	}

	visitor.NotifyAllDownloads(true);

	{
		std::unique_lock<std::mutex> lck(_logWindowMutex);

		using r = Launcher::RepentogonInstaller;
		r::DownloadInstallRepentogonResult result = future.get();

		switch (result) {
		case r::DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES:
			_logWindow->SetForegroundColour(*wxRED);
			_logWindow->AppendText("Error while checking availability of updates, "
				"check the log file for more details\n");
			break;

		case r::DOWNLOAD_INSTALL_REPENTOGON_ERR:
			_logWindow->SetForegroundColour(*wxRED);
			_logWindow->AppendText("Error while performing the installation of Repentogon, "
				"check the log file for more details\n");
			break;

		case r::DOWNLOAD_INSTALL_REPENTOGON_UTD:
			_logWindow->AppendText("Repentogon is up-to-date\n");
			break;

		case r::DOWNLOAD_INSTALL_REPENTOGON_OK:
			_logWindow->AppendText("Repentogon successfully installed\n");
			break;

		default:
			wxASSERT(false);
		}
	}

	{
		std::unique_lock<std::mutex> lck(_downloadMutex);
		_downloadInProgress = false;
		_cancelRequested = false;
		_terminateDownload = false;
	}

	if (!sCLI->RepentogonInstallerWait()) {
		std::this_thread::sleep_for(std::chrono::seconds(2));
		Close(true);
	}
}