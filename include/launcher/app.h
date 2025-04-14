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
		bool RunWizard(bool* installedRepentogon);
		RepentogonInstallerFrame* CreateRepentogonInstallerWindow(bool forceUpdate);
		MainFrame* CreateMainWindow();
	};
}