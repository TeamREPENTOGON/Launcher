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
#include "launcher/standalone_rgon_folder.h"
#include "shared/externals.h"
#include "shared/logger.h"
#include "shared/filesystem.h"
#include "shared/scoped_file.h"
#include "shared/sha256.h"

namespace fs = std::filesystem;

namespace Launcher {
	Installation::Installation(ILoggableGUI* gui, LauncherConfiguration* configuration) : _gui(gui),
		_repentogonInstallation(gui), _isaacInstallation(gui),
		_launcherConfiguration(configuration) {
	}

	std::optional<std::string> Installation::LocateIsaac(std::optional<std::string> const& isaacPath,
		bool* standalone) {
		if (isaacPath) {
			if (_isaacInstallation.Validate(*isaacPath, standalone)) {
				return isaacPath;
			} else {
				Logger::Warn("Manually provided Isaac path %s is not a valid Isaac installation, "
					"attempting detection from configuration\n", isaacPath->c_str());
			}
		}

		if (_launcherConfiguration->Loaded()) {
			std::string result = _launcherConfiguration->IsaacExecutablePath();
			if (_isaacInstallation.Validate(result, standalone)) {
				return result;
			} else if (_launcherConfiguration->IsaacExecutablePathHasOverride()) {
				Logger::Warn("Isaac path provided via command-line %s is not a valid Isaac installation\n", result.c_str());
				return std::nullopt;
			} else {
				Logger::Warn("Configured Isaac path %s is not a valid Isaac installation, "
					"attempting to auto-detect\n", result.c_str());
			}
		}

		std::optional<std::string> result = _isaacInstallation.AutoDetect(standalone);
		if (!result) {
			Logger::Error("Unable to auto-detect Isaac installation\n");
		}

		return result;
	}

	

	bool Installation::CheckRepentogonInstallation() {
		if (!_isaacInstallation.GetMainInstallation().IsValid()) {
			_repentogonInstallation.Invalidate();
			return false;
		}

		fs::path path;
		std::string const& folder = _isaacInstallation.GetMainInstallation()
			.GetFolderPath();

		if (!standalone_rgon::GenerateRepentogonPath(folder, path, true)) {
			Logger::Fatal("Installation::CheckRepentogonInstallation: unable to generate "
				"Repentogon path from Isaac folder %s\n", folder.c_str());
			std::terminate();
		}

		if (_repentogonInstallation.Validate(path.string())) {
			return _isaacInstallation.ValidateRepentogon(path.string());
		} else {
			return false;
		}
	}

	std::tuple<std::optional<std::string>, bool> Installation::Initialize(
		std::optional<std::string> const& isaacPath, bool* standalone) {
		std::optional<std::string> locatedIsaacPath = LocateIsaac(isaacPath, standalone);
		if (locatedIsaacPath) {
			_launcherConfiguration->SetIsaacExecutablePath(
				_isaacInstallation.GetMainInstallation().GetExePath());
		}
		bool repentogonOk = locatedIsaacPath ? CheckRepentogonInstallation() : false;

		return std::make_tuple(locatedIsaacPath, repentogonOk);
	}

	int Installation::SetIsaacExecutable(std::string const& file, bool* standalone) {
		bool ok = _isaacInstallation.Validate(file, standalone);
		if (ok) {
			_launcherConfiguration->SetIsaacExecutablePath(
				_isaacInstallation.GetMainInstallation().GetExePath());
			if (CheckRepentogonInstallation()) {
				return BothInstallationsValid;
			}

			return IsaacInstallationValid;
		}

		return ok ? IsaacInstallationValid : 0;
	}

	void Installation::CheckLegalIsaacPath(const std::string& isaacExePath) {
		if (!isaacExePath.empty() && std::filesystem::exists(isaacExePath)) {
			const std::filesystem::path launcherDir = std::filesystem::current_path();
			const std::filesystem::path isaacDir = std::filesystem::path(isaacExePath).parent_path();
			const std::filesystem::path rgonDir = isaacDir / "Repentogon";
			try {
				if (std::filesystem::equivalent(launcherDir, isaacDir) || (std::filesystem::exists(rgonDir) && std::filesystem::equivalent(launcherDir, rgonDir))) {
					MessageBoxA(NULL, "The REPENTOGON Launcher is in an invalid location!"
						"\n\nIt cannot be placed directly into Isaac's installation folder, nor in a subfolder named \"Repentogon\", as that directory is required for the installation of REPENTOGON."
						"\n\nPlease extract the REPENTOGON Launcher files to a different location (such as in a folder called \"REPENTOGONLauncher\") instead.",
						"REPENTOGON Launcher", MB_ICONERROR);
					Logger::Fatal("The launcher was placed at an invalid location @ `%s`!\n", launcherDir.string().c_str());
				}
			} catch (const std::filesystem::filesystem_error& ex) {
				Logger::Error("Installation::CheckLegalIsaacPath failed to check `%s` with error: %s", isaacExePath.c_str(), ex.what());
			}
		}
	};
}