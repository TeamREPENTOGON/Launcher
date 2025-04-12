#pragma once

#include <atomic>
#include <mutex>

#include <wx/frame.h>

#include "launcher/installation.h"

// Forward decl to avoid circular depdency
namespace Launcher {
	class MainFrame;
}

class RepentogonInstallerFrame : public wxFrame {
public:
	RepentogonInstallerFrame(Launcher::Installation* installation, bool forceUpdate);
	virtual ~RepentogonInstallerFrame();

	void Initialize();
	void InstallRepentogon();
	void OnClose(wxCloseEvent& event);

	inline void SetMainFrame(Launcher::MainFrame* frame) {
		_mainFrame = frame;
	}

	inline Launcher::MainFrame* GetMainFrame() const {
		return _mainFrame;
	}

private:
	void InstallRepentogonThread();
	bool RequestCancel();
	void RequestReCancel();

	/* Pointer to the main window.
	 * 
	 * If this is not null, then the main window is set visible when this window
	 * is closed.
	 * 
	 * This is used in the initialization to have both windows exist in parallel.
	 * Upon completion of the installation, this window transfers control to
	 * the main window.
	 */
	Launcher::MainFrame* _mainFrame = nullptr;
	wxTextCtrl* _logWindow = nullptr;
	bool _downloadInProgress = false;
	bool _forceUpdate = false;

	/* How many times a cancel was requested. */
	uint32_t _cancelRequested = 0;
	/* Set to true once the download thread has been joined and the main 
	 * window (if any) has been set to visible. Further close requests will
	 * not go through our handler if this is set to true.
	 */
	bool _closing = false;
	/* Forcibly terminate the download. This acts as a last resort option in
	 * case the downloader gets stuck.
	 */
	std::atomic<bool> _terminateDownload = false;
	/* Point at which the cancel request started. If the user requests cancelation
	 * several times, this is used to configure a threshold after which the
	 * cancelation turns into a termination.
	 */
	std::chrono::steady_clock::time_point _cancelStart;

	std::thread _downloadThread;
	std::mutex _downloadMutex;
	std::mutex _logWindowMutex;

	Launcher::Installation* _installation = nullptr;

	wxDECLARE_EVENT_TABLE();
};