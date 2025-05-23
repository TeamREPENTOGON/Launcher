#include <WinSock2.h>
#include <Windows.h>
#include <bcrypt.h>
#include <io.h>
#include <Psapi.h>
#include <UserEnv.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>

#include <cwchar>

#include "inih/cpp/INIReader.h"
#include "launcher/installation.h"
#include "shared/externals.h"
#include "shared/logger.h"
#include "shared/filesystem.h"
#include "shared/scoped_file.h"
#include "shared/sha256.h"

namespace Launcher {
	Installation::Installation(ILoggableGUI* gui, LauncherConfiguration* configuration) : _gui(gui),
		_repentogonInstallation(gui), _isaacInstallation(gui),
		_launcherConfiguration(configuration) {
	}

	std::optional<std::string> Installation::LocateIsaac(std::optional<std::string> const& isaacPath) {
		if (isaacPath) {
			if (_isaacInstallation.Validate(*isaacPath)) {
				return isaacPath;
			} else {
				Logger::Warn("Manually provided Isaac path %s is not a valid Isaac installation, "
					"attempting detection from configuration\n", isaacPath->c_str());
			}
		}

		if (_launcherConfiguration->Loaded()) {
			std::string result = _launcherConfiguration->IsaacExecutablePath();
			if (_isaacInstallation.Validate(result)) {
				return result;
			} else {
				Logger::Warn("Configured Isaac path %s is not a valid Isaac installation, "
					"attempting to auto-detect\n", result.c_str());
			}
		}

		std::optional<std::string> result = _isaacInstallation.AutoDetect();
		if (!result) {
			Logger::Error("Unable to auto-detect Isaac installation\n");
		}

		return result;
	}

	bool Installation::CheckRepentogonInstallation() {
		if (!_isaacInstallation.IsValid()) {
			_repentogonInstallation.Invalidate();
			return false;
		}

		return _repentogonInstallation.Validate(_isaacInstallation.GetFolderPath());
	}

	std::tuple<std::optional<std::string>, bool> Installation::Initialize(
		std::optional<std::string> const& isaacPath) {
		std::optional<std::string> locatedIsaacPath = LocateIsaac(isaacPath);
		if (locatedIsaacPath) {
			_launcherConfiguration->IsaacExecutablePath(_isaacInstallation.GetExePath());
		}
		bool repentogonOk = locatedIsaacPath ? CheckRepentogonInstallation() : false;

		return std::make_tuple(locatedIsaacPath, repentogonOk);
	}

	int Installation::SetIsaacExecutable(std::string const& file) {
		bool ok = _isaacInstallation.Validate(file);
		if (ok) {
			_launcherConfiguration->IsaacExecutablePath(_isaacInstallation.GetExePath());
			if (CheckRepentogonInstallation()) {
				return BothInstallationsValid;
			}

			return IsaacInstallationValid;
		}

		return ok ? IsaacInstallationValid : 0;
	}
}