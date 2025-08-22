#pragma once

#include <memory>
#include <string>

#include "inih/cpp/INIReader.h"
#include "shared/loggable_gui.h"

enum LauncherConfigurationInitialize {
	LAUNCHER_CONFIGURATION_INIT_NO_USERPROFILE,
	LAUNCHER_CONFIGURATION_INIT_GET_USER_DIRECTORY,
	LAUNCHER_CONFIGURATION_INIT_HIERARCHY,
	LAUNCHER_CONFIGURATION_INIT_INVALID_PATH
};

enum LauncherConfigurationLoad {
	LAUNCHER_CONFIGURATION_LOAD_OPEN,
	LAUNCHER_CONFIGURATION_LOAD_PARSE_ERROR,
	LAUNCHER_CONFIGURATION_LOAD_NO_ISAAC
};

enum LaunchMode {
	LAUNCH_MODE_VANILLA,
	LAUNCH_MODE_REPENTOGON
};

struct Options {
	/* General options */
	std::optional<std::string> isaacExecutablePath;
	/**
	 * Indicate whether the wizard ran to the point where the user selected
	 * their options when it comes to the configuration of Repentogon (i.e.
	 * automatic updates and unstable updates).
	 *
	 * This variable does NOT indicate whether the wizard ran successfully.
	 * Only the above.
	 */
	std::optional<bool> ranWizard;

	/* Shared options */
	std::optional<LaunchMode> launchMode;
	std::optional<bool> hideWindow;

	/* Repentogon options */
	std::optional<bool> console;
	std::optional<bool> automaticUpdates;
	std::optional<bool> unstableUpdates;

	/* Game options */
	std::optional<bool> luaDebug;
	std::optional<long> levelStage;
	std::optional<long> stageType;
	std::optional<std::string> luaHeapSize;
	std::optional<std::string> loadRoom;
};

namespace Configuration::Defaults {
	const std::string isaacExecutablePath = "";
	constexpr const bool ranWizard = false;
	constexpr const LaunchMode launchMode = LAUNCH_MODE_REPENTOGON;
	constexpr const bool hideWindow = true;
	constexpr const bool console = false;
	constexpr const bool automaticUpdates = true;
	constexpr const bool unstableUpdates = false;
	constexpr const bool luaDebug = false;
	constexpr const int levelStage = 0;
	constexpr const int stageType = 0;
	const std::string luaHeapSize = "1024M";
	const std::string loadRoom = "";
}

namespace Configuration::Sections {
	const std::string general("General");
	const std::string repentogon("Repentogon");
	const std::string vanilla("Vanilla");
	const std::string shared("Shared");
}

namespace Configuration::Keys {
	const std::string isaacExecutableKey("IsaacExecutable");
	const std::string ranWizard("RanWizard");

	const std::string launchMode("LaunchMode");
	const std::string hideWindow("HideWindow");

	const std::string levelStage("LevelStage");
	const std::string luaDebug("LuaDebug");
	const std::string luaHeapSize("LuaHeapSize");
	const std::string stageType("StageType");
	const std::string loadRoom("LoadRoom");

	const std::string console("Console");
	const std::string update("Update");
	const std::string unstableUpdates("UnstableUpdates");
}

#undef CONFIGURATION_FIELD
#define CONFIGURATION_FIELD(T, NAME, FIELD)\
inline const T NAME##IgnoreOverride() const {\
	return _options.FIELD.value_or(Configuration::Defaults::FIELD);\
}\
inline const T NAME() const {\
	if (_cliOverrides.FIELD.has_value()) {\
		return *_cliOverrides.FIELD;\
	}\
	return NAME##IgnoreOverride();\
}\
inline void NAME(const std::optional<T> value) {\
	if (NAME##HasCliOverride()) {\
		_cliOverrides.FIELD = value;\
	} else {\
		_options.FIELD = value;\
	}\
}\
inline bool NAME##HasCliOverride() const {\
	return _cliOverrides.FIELD.has_value();\
}

class LauncherConfiguration {
public:
	LauncherConfiguration();

	static bool InitializeConfigurationPath(LauncherConfigurationInitialize* result,
		std::optional<std::string> const& hint);

	static inline bool WasConfigurationPathInitialized() {
		return !_configurationPath.empty();
	}

	static inline const char* GetConfigurationPath() {
		return _configurationPath.c_str();
	}

	inline bool Loaded() const {
		return _isLoaded;
	}

	/**
	 * Load the configuration file.
	 *
	 * @pre InitializeConfigurationPath must have been called and its return
	 * value must have been @a true.
	 *
	 * The function returns true if the configuration file is successfully
	 * opened, parsed and there is an entry for the Isaac installation
	 * inside it. The validity of the Isaac installation is **NOT** checked.
	 *
	 * Additional information can be found in @a result if the function fails.
	 */
    bool Load(LauncherConfigurationLoad* result);

	void Write();

	CONFIGURATION_FIELD(std::string, IsaacExecutablePath, isaacExecutablePath);
	CONFIGURATION_FIELD(LaunchMode, IsaacLaunchMode, launchMode);
	CONFIGURATION_FIELD(bool, RepentogonConsole, console);
	CONFIGURATION_FIELD(bool, AutomaticUpdates, automaticUpdates);
	CONFIGURATION_FIELD(bool, UnstableUpdates, unstableUpdates);
	CONFIGURATION_FIELD(bool, LuaDebug, luaDebug);
	CONFIGURATION_FIELD(bool, HideWindow, hideWindow);
	CONFIGURATION_FIELD(bool, RanWizard, ranWizard);
	CONFIGURATION_FIELD(long, Stage, levelStage);
	CONFIGURATION_FIELD(long, StageType, stageType);
	CONFIGURATION_FIELD(std::string, LoadRoom, loadRoom);
	CONFIGURATION_FIELD(std::string, LuaHeapSize, luaHeapSize);

private:
	bool Search(LauncherConfigurationLoad* result);
	bool Process(LauncherConfigurationLoad* result);
	bool CheckConfigurationFileExists();
	void Load(INIReader const& reader);
	void LoadFromFile(INIReader const& reader);
	void LoadFromCLI();

	/* Path to the folder containing the configuration of the launcher. */
	static std::string _configurationPath;
	/* Eventual line in the launcher configuration file that resulted
	 * in a parse error.
	 */
	int _configFileParseErrorLine = 0;
	bool _isLoaded = false;

	Options _options;
	Options _cliOverrides;
};

#undef CONFIGURATION_FIELD