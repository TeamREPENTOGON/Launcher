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

namespace Configuration::Defaults {
	constexpr const bool console = false;
	constexpr const int levelStage = 0;
	constexpr const int stageType = 0;
	constexpr const bool luaDebug = false;
	const std::string luaHeapSize("1024M");
	constexpr const bool update = true;
	constexpr const bool unstableUpdates = false;
	constexpr const bool hideWindow = true;
	constexpr const bool ranWizard = false;
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
	const std::string roomId("RoomID");

	const std::string console("Console");
	const std::string update("Update");
	const std::string unstableUpdates("UnstableUpdates");

}

#undef CONFIGURATION_FIELD
#define CONFIGURATION_FIELD(T, NAME, FIELD) inline T const& NAME() const {\
	return FIELD;\
}\
\
inline void NAME(T const& param) {\
		FIELD = param;\
}

#undef OPTIONAL_CONFIGURATION_FIELD
#define OPTIONAL_CONFIGURATION_FIELD(T, NAME, FIELD) inline T const& NAME() const {\
	return *FIELD;\
}\
\
inline void NAME(T const& param) {\
	FIELD = param;\
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

	CONFIGURATION_FIELD(std::string, IsaacExecutablePath, _isaacExecutablePath);
	CONFIGURATION_FIELD(LaunchMode, IsaacLaunchMode, _launchMode);
	CONFIGURATION_FIELD(bool, UnstableUpdates, _unstableUpdates);
	CONFIGURATION_FIELD(bool, RepentogonConsole, _repentogonConsole);
	CONFIGURATION_FIELD(bool, AutomaticUpdates, _update);
	CONFIGURATION_FIELD(bool, LuaDebug, _luaDebug);
	CONFIGURATION_FIELD(bool, HideWindow, _hideWindow);
	CONFIGURATION_FIELD(bool, RanWizard, _ranWizard);
	OPTIONAL_CONFIGURATION_FIELD(long, Stage, _stage);
	OPTIONAL_CONFIGURATION_FIELD(long, StageType, _stageType);
	OPTIONAL_CONFIGURATION_FIELD(long, RoomID, _roomId);
	OPTIONAL_CONFIGURATION_FIELD(std::string, LuaHeapSize, _luaHeapSize);

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

	/* General options */
	std::string _isaacExecutablePath;
	/**
	 * Indicate whether the wizard ran to the point where the user selected
	 * their options when it comes to the configuration of Repentogon (i.e.
	 * automatic updates and unstable updates).
	 *
	 * This variable does NOT indicate whether the wizard ran successfully.
	 * Only the above.
	 */
	bool _ranWizard = false;

	/* Shared options */
	LaunchMode _launchMode = LAUNCH_MODE_REPENTOGON;
	bool _hideWindow = true;

	/* Repentogon options */
	bool _unstableUpdates = false;
	bool _repentogonConsole = false;
	bool _update = true;

	/* Game options */
	bool _luaDebug = false;
	std::optional<long> _stage;
	std::optional<long> _stageType;
	std::optional<std::string> _luaHeapSize;
	std::optional<long> _roomId;
};

#undef CONFIGURATION_FIELD