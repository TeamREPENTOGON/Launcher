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

	std::optional<std::string> Installation::LocateIsaac() {
		if (_launcherConfiguration->Loaded()) {
			std::string result = _launcherConfiguration->GetIsaacExecutablePath();
			if (_isaacInstallation.Validate(result)) {
				return result;
			}
		}

		return _isaacInstallation.AutoDetect();
	}

	bool Installation::CheckRepentogonInstallation() {
		if (!_isaacInstallation.IsValid()) {
			return false;
		}

		return _repentogonInstallation.Validate(_isaacInstallation.GetFolderPath());
	}

	std::tuple<std::optional<std::string>, bool> Installation::Initialize() {
		std::optional<std::string> isaacPath = LocateIsaac();
		bool repentogonOk = isaacPath ? CheckRepentogonInstallation() : false;

		return std::make_tuple(isaacPath, repentogonOk);
	}

	bool Installation::SetIsaacExecutable(std::string const& file) {
		bool ok = _isaacInstallation.Validate(file);
		if (ok) {
			CheckRepentogonInstallation();
		}

		return ok;
	}

	/* void Installation::WriteLauncherConfigurationFile() {
		if (_configurationPath.empty()) {
			Logger::Fatal("Installation::WriteLauncherConfigurationFile called, but configuration path was not properly initialized before\n");
			throw std::runtime_error("Inconsistent runtime state: attempted to write launcher configuration without a valid path");
		}

		if (_isaacExecutable.empty()) {
			Logger::Fatal("Installation::WriteLauncherConfigurationFile: _isaacExecutable is empty\n");
			throw std::runtime_error("Inconsistent runtime state: attempted to write launcher configuration without a valid Isaac folder");
		}

		std::ofstream configuration(_configurationPath, std::ios::out);
		configuration << "[" << Configuration::GeneralSection << "]" << std::endl;
		configuration << Configuration::IsaacExecutableKey << " = " << _isaacExecutable << std::endl;
		configuration.close();

		_configurationFile.reset(new INIReader(_configurationPath));
		if (int line = _configurationFile->ParseError(); line != 0) {
			Logger::Fatal("Installation::WriteLauncherConfigurationFile: error while rereading configuration file\n");
			if (line == -1) {
				Logger::Fatal("Unable to open configuration file %s\n", _configurationPath.c_str());
			} else {
				Logger::Fatal("Parse error on line %d of configuration file %s\n", line, _configurationPath.c_str());
			}
			throw std::runtime_error("Error while writing configuration file");
		}
	} */
}