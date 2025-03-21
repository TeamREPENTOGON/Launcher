#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include "launcher/isaac_installation.h"
#include "launcher/launcher_configuration.h"
#include "launcher/repentogon_installation.h"
#include "shared/loggable_gui.h"

namespace Launcher {
	class Installation {
	public:
		Installation(ILoggableGUI* gui, LauncherConfiguration* launcherConfiguration);

		std::optional<std::string> LocateIsaac();
		bool CheckRepentogonInstallation();

		std::tuple<std::optional<std::string>, bool> Initialize();

		inline RepentogonInstallation const& GetRepentogonInstallation() const {
			return _repentogonInstallation;
		}

		inline IsaacInstallation const& GetIsaacInstallation() const {
			return _isaacInstallation;
		}

		inline LauncherConfiguration* GetLauncherConfiguration() const {
			return _launcherConfiguration;
		}

		inline void SetGUI(ILoggableGUI* gui) {
			_gui = gui;
		}

		bool SetIsaacExecutable(std::string const& file);

	private:
		ILoggableGUI* _gui = nullptr;
		RepentogonInstallation _repentogonInstallation;
		IsaacInstallation _isaacInstallation;
		LauncherConfiguration* _launcherConfiguration;
	};
}