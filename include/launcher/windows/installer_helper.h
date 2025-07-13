#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "launcher/installation.h"
#include "launcher/repentogon_installer.h"

#include "wx/textctrl.h"

class RepentogonInstallerHelper {
public:
    RepentogonInstallerHelper(wxWindow* parent, Launcher::Installation* installation,
        bool allowUnstable, bool forceUpdate, wxTextCtrl* _logWindow);
    ~RepentogonInstallerHelper();

    RepentogonInstallerHelper(RepentogonInstallerHelper const&) = delete;
    RepentogonInstallerHelper(RepentogonInstallerHelper&&) = delete;

    RepentogonInstallerHelper& operator=(RepentogonInstallerHelper const&) = delete;
    RepentogonInstallerHelper& operator=(RepentogonInstallerHelper&&) = delete;

    enum InstallationStatus {
        STATUS_NONE,
        STATUS_IN_PROGRESS,
        STATUS_FINISHED,
        STATUS_CANCELLED
    };

    enum TerminateResult {
        TERMINATE_CANCEL_ACCEPTED,
        TERMINATE_CANCEL_REJECTED,
        TERMINATE_CANCEL_RECANCEL,
        TERMINATE_CANCEL_FORCE,
        TERMINATE_OK
    };

    inline bool HasCompleted(bool allowCancel) const {
        std::unique_lock<std::mutex> lck(_terminationMutex);
        return _status == STATUS_FINISHED || (allowCancel && _status == STATUS_CANCELLED);
    }

    inline bool HasStarted() const {
        std::unique_lock<std::mutex> lck(_terminationMutex);
        return _status != STATUS_NONE;
    }

    void Install(std::function<void(bool, Launcher::RepentogonInstaller::DownloadInstallRepentogonResult)> callback);
    TerminateResult Terminate();

    void Wait();

private:
    bool Cancel();
    bool ReCancel();
    void TerminateInternal();
    void HandleThreadTermination() noexcept;

    void InstallerThread();

    wxWindow* _parent = nullptr;
    Launcher::Installation* _installation = nullptr;
    uint32_t _cancelCount = 0;
    std::chrono::steady_clock::time_point _cancelStart;
    bool _allowUnstable = false;
    bool _forceUpdate = false;
    bool _forceCancel = false;
    InstallationStatus _status = STATUS_NONE;
    std::thread _installationThread;
    /* Need mutual exclusion because multiple threads may log in the window,
     * for example when a cancel is being processed.
     */
    std::mutex _logWindowMutex;
    wxTextCtrl* _logWindow = nullptr;

    mutable std::mutex _terminationMutex;

    std::function<void(bool, Launcher::RepentogonInstaller::DownloadInstallRepentogonResult)> _callback;
};