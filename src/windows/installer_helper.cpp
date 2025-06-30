#include "launcher/windows/installer_helper.h"

#include "launcher/cli.h"
#include "launcher/log_helpers.h"
#include "launcher/repentogon_installer.h"

#include "shared/logger.h"

#include "wx/msgdlg.h"

RepentogonInstallerHelper::RepentogonInstallerHelper(wxWindow* parent,
    Launcher::Installation* installation, bool allowUnstable, bool forceUpdate,
    wxTextCtrl* logWindow) :
_parent(parent), _installation(installation), _allowUnstable(allowUnstable),
_forceUpdate(forceUpdate), _logWindow(logWindow) {

}

RepentogonInstallerHelper::~RepentogonInstallerHelper() {
    HandleThreadTermination();
}

void RepentogonInstallerHelper::HandleThreadTermination() noexcept {
    if (!_installationThread.joinable()) {
        return;
    }

	std::unique_lock<std::mutex> lck(_terminationMutex);

	switch (_status) {
	case STATUS_NONE:
	case STATUS_IN_PROGRESS:
		Logger::Fatal("RepentogonInstallerHelper: destructor called with invalid status %d\n", _status);
		std::terminate();
		return;

	default:
		if (std::this_thread::get_id() == _installationThread.get_id()) {
			Logger::Warn("RepentogonInstallerHelper: destructor called from within the installer thread\n");
			_installationThread.detach();
		} else {
			Logger::Info("RepentogonInstallerHelper: joining installation thread\n");
			_installationThread.join();
			Logger::Info("RepentogonInstallerHelper: joined installation thread\n");
		}
		break;
	}
}

RepentogonInstallerHelper::TerminateResult RepentogonInstallerHelper::Terminate() {
	std::unique_lock<std::mutex> lck(_terminationMutex);

	switch (_status) {
	case STATUS_NONE:
		_status = STATUS_CANCELLED;
		return TERMINATE_OK;

	case STATUS_CANCELLED:
		if (ReCancel()) {
			return TERMINATE_CANCEL_RECANCEL;
		} else {
			return TERMINATE_CANCEL_FORCE;
		}

	case STATUS_FINISHED:
		TerminateInternal();
		return TERMINATE_OK;

	case STATUS_IN_PROGRESS:
		if (Cancel()) {
			return TERMINATE_CANCEL_ACCEPTED;
		} else {
			return TERMINATE_CANCEL_REJECTED;
		}

	default:
		wxASSERT(false);
		__assume(0);
	}

	__assume(0);
	return TERMINATE_OK;
}

void RepentogonInstallerHelper::TerminateInternal() {

}

bool RepentogonInstallerHelper::Cancel() {
	if (wxMessageBox("A Repentogon download is in progress. Are you sure you want to cancel ?",
		"Warning", wxCENTRE | wxICON_WARNING | wxYES_NO, _parent) == wxNO)
	{
		Logger::Info("RepentogonInstallerHelper::Cancel: user rejected cancel\n");
		return false;
	}

	Logger::Info("RepentogonInstallerHelper::Cancel: user accepted cancel\n");
	std::unique_lock<std::mutex> lck(_logWindowMutex);
	_logWindow->AppendText("Cancel request registered\n");
	_status = STATUS_CANCELLED;
	_cancelCount = 1;
	_cancelStart = std::chrono::steady_clock::now();

	return true;
}

bool RepentogonInstallerHelper::ReCancel() {
	wxString s;

	++_cancelCount;
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (_cancelCount >= 5 ||
		std::chrono::duration_cast<std::chrono::seconds>(now - _cancelStart).count() >= 5) {
		_forceCancel = true;

		Logger::Warn("RepentogonInstallerHelper::ReCancel: cancel timedout (_cancelCount = %d)\n", _cancelCount);
		s.Printf("[WARN] Too many cancel requests received / spent too much time waiting for cancel to be effective, forcibly stopping\n");
	} else {
		Logger::Warn("RepentogonInstallerHelper::ReCancel: _cancelCount = %d\n", _cancelCount);
		s.Printf("[WARN] Received %u cancel requests\n", _cancelCount);
	}

	std::unique_lock<std::mutex> lck(_logWindowMutex);
	_logWindow->AppendText(s);
	return !_forceCancel;
}

void RepentogonInstallerHelper::Install(std::function<void(bool)> callback) {
	std::unique_lock<std::mutex> lck(_terminationMutex);

	if (_status != STATUS_NONE) {
		Logger::Fatal("RepentogonInstallerHelper::Install: install already started\n");
		std::terminate();
		return;
	}

	_callback = std::move(callback);
	_installationThread = std::thread(&RepentogonInstallerHelper::InstallerThread, this);
}

void RepentogonInstallerHelper::InstallerThread() {
	{
		std::unique_lock<std::mutex> lck(_terminationMutex);
		if (_status != STATUS_NONE) {
			if (_status == STATUS_CANCELLED) {
				_callback(false);
				return;
			}

			Logger::Fatal("RepentogonInstallHelper::Install: download already in progress (%d) !",
				_status);
			std::terminate();
			return;
		}

		_status = STATUS_IN_PROGRESS;
	}

	Launcher::RepentogonInstaller installer(_installation);
	auto [future, monitor] = installer.InstallLatestRepentogon(
		_forceUpdate, _allowUnstable);
	bool shouldContinue = true;
	NotificationVisitor visitor(_logWindow, sCLI->RepentogonInstallerRefreshRate());
	while (shouldContinue) {
		{
			std::unique_lock<std::mutex> lck(_terminationMutex);
			if (_cancelCount) {
				break;
			}
		}

		if (future.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
			shouldContinue = false;
		}

		while (std::optional<Launcher::RepentogonInstallationNotification> notification = monitor->Get()) {
			// Sub-efficient, but less likely to cause a cancellation request to be missed
			std::unique_lock<std::mutex> lck(_logWindowMutex);
			std::visit(visitor, notification->_data);
		}
	}

	{
		std::unique_lock<std::mutex> lck(_terminationMutex);
		if (_cancelCount && shouldContinue) {
			installer.CancelInstallation();
			_logWindow->AppendText("Download thread observed cancellation request\n");
			Logger::Info("RepentogonInstallerHelper::Install: cancel requested\n");
			_callback(false);
			return;
		}

		while (std::optional<Launcher::RepentogonInstallationNotification> notification = monitor->Get()) {
			// Sub-efficient, but less likely to cause a cancellation request to be missed
			std::unique_lock<std::mutex> lck(_logWindowMutex);
			std::visit(visitor, notification->_data);
		}

		_status = STATUS_FINISHED;
	}

	{
		std::unique_lock<std::mutex> lck(_logWindowMutex);

		visitor.NotifyAllDownloads(false);
		using r = Launcher::RepentogonInstaller;
		r::DownloadInstallRepentogonResult result = future.get();

		switch (result) {
		case r::DOWNLOAD_INSTALL_REPENTOGON_ERR_CHECK_UPDATES: {
			_logWindow->SetForegroundColour(*wxRED);
			_logWindow->AppendText("Error while checking availability of updates, "
				"check the log file for more details\n");
			break;
		}

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

		_installation->CheckRepentogonInstallation();
		_callback(true);
	}
}