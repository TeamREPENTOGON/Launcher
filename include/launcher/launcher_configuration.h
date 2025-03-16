#pragma once

#include <memory>
#include <string>

#include "inih/cpp/INIReader.h"
#include "shared/loggable_gui.h"

enum LauncherConfigurationLoad {
	LAUNCHER_CONFIGURATION_LOAD_NO_USERPROFILE,
	LAUNCHER_CONFIGURATION_LOAD_GET_USER_DIRECTORY,
	LAUNCHER_CONFIGURATION_LOAD_HIERARCHY,
	LAUNCHER_CONFIGURATION_LOAD_NOT_FOUND,
	LAUNCHER_CONFIGURATION_LOAD_OPEN,
	LAUNCHER_CONFIGURATION_LOAD_PARSE_ERROR
};

namespace Launcher::Configuration {
	static constexpr const char* GeneralSection = "General";
	static constexpr const char* IsaacExecutableKey = "IsaacExecutable";
	static constexpr const char* EmptyPath = "";
}

class LauncherConfiguration {
public:
	LauncherConfiguration();

	inline bool Loaded() const {
		return _isLoaded;
	}

    bool Load(LauncherConfigurationLoad* result);
	inline INIReader* GetReader() const {
		return _configurationFile.get();
	}

	inline std::string const& GetConfigurationPath() const {
		return _configurationPath;
	}

	std::string GetIsaacExecutablePath();

private:
	bool Search(LauncherConfigurationLoad* result);
	bool Process(LauncherConfigurationLoad* result);

	/* Path to the folder containing the configuration of the launcher. */
	std::string _configurationPath;
	/* Eventual line in the launcher configuration file that resulted
	 * in a parse error.
	 */
	int _configFileParseErrorLine = 0;
	std::unique_ptr<INIReader> _configurationFile;
	bool _isLoaded = false;
};