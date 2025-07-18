#pragma once

#include "launcher/windows/launcher.h"
#include "launcher/windows/repentogon_installer.h"

namespace Launcher {
	class MainFrame;

	int Launch(ILoggableGUI* gui, const char* path, bool isLegacy, LauncherConfiguration const* configuration);

	class App : public wxApp {
	public:
		bool OnInit() override;
		int OnExit() override;

	private:
		bool RunWizard(MainFrame* mainWindow, bool* installedRepentogon);
		RepentogonInstallerFrame* CreateRepentogonInstallerWindow(
			MainFrame* mainWindow, bool forceUpdate, bool allowUnstable);
		MainFrame* CreateMainWindow();

		MainFrame* _mainFrame;
		std::thread _initThread;
	};
}