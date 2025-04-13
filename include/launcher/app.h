#pragma once

#include "launcher/windows/launcher.h"
#include "launcher/windows/repentogon_installer.h"

namespace Launcher {
	class MainFrame;

	class App : public wxApp {
	public:
		bool OnInit() override;
		int OnExit() override;
		bool RunWizard(bool* installedRepentogon);
		RepentogonInstallerFrame* CreateRepentogonInstallerWindow(bool forceUpdate);
		MainFrame* CreateMainWindow();
	};
}