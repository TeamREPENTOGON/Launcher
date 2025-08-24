#pragma once

#include "launcher/windows/launcher.h"
#include "launcher/windows/repentogon_installer.h"

#define IDI_ICON1                       101

namespace Launcher {
	class LauncherMainWindow;

	DWORD Launch(ILoggableGUI* gui, const char* path, bool isLegacy,
		LauncherConfiguration const* configuration);

	class App : public wxApp {
	public:
		bool OnInit() override;
		int OnExit() override;

	private:
		bool RunWizard(LauncherMainWindow* mainWindow, bool* installedRepentogon);
		RepentogonInstallerFrame* CreateRepentogonInstallerWindow(
			LauncherMainWindow* mainWindow, bool forceUpdate, bool allowUnstable);
		LauncherMainWindow* CreateMainWindow();

		LauncherMainWindow* _mainFrame;
	};
}