#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <wx/frame.h>

#include "launcher/installation.h"
#include "launcher/windows/installer_helper.h"

// Forward decl to avoid circular dependency
namespace Launcher {
	class MainFrame;
}

class RepentogonInstallerFrame : public wxFrame {
public:
	RepentogonInstallerFrame(Launcher::Installation* installation, bool forceUpdate,
		bool allowUnstable);
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
	void OnRepentogonInstalled(bool finished,
		Launcher::RepentogonInstaller::DownloadInstallRepentogonResult result);

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
	bool _forceUpdate = false;
	bool _allowUnstable = false;

	std::mutex _installerMutex;
	std::unique_ptr<RepentogonInstallerHelper> _helper;

	Launcher::Installation* _installation = nullptr;

	wxDECLARE_EVENT_TABLE();
};