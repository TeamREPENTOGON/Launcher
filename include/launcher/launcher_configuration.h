#pragma once

#include <memory>
#include <string>

#include "steam_api.h"
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

/**
 * Each option must have 3 things:
 * 
 * 1. Add it to the "Options" struct. It MUST be std::optional wrapped.
 * 2. Add a default value for it to the Configuration::Defaults namespace.
 * 3. Add functions to the LauncherConfiguration class using the CONFIGURATION_FIELD macro.
 * 
 * The exact same field name must be used in all three locations.
 */

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
	std::optional<bool> stealthMode;

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
	constexpr const bool stealthMode = false;
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
	const std::string stealthMode("StealthMode");

	const std::string levelStage("LevelStage");
	const std::string luaDebug("LuaDebug");
	const std::string luaHeapSize("LuaHeapSize");
	const std::string stageType("StageType");
	const std::string loadRoom("LoadRoom");

	const std::string console("Console");
	const std::string update("Update");
	const std::string unstableUpdates("UnstableUpdates");
}

/**
 * On "Overrides":
 * 
 * If an "override" value is set for an option, that value will override
 * whatever value was loaded from the configuration file, but will NOT
 * be written back to the configuration file. Typically, this is used
 * for when options are set via the command line so that those values
 * are not saved back to the user's config file, but can be used for
 * other things if we want to "lock" an option without modifying the
 * user's settings in their configuration file.
 *
 * It is expected that overridden options will have their respective
 * UI elements locked so that they cannot be changed by the user.
 * However, if the value is changed somehow, it will just change the
 * current value of the override and will still not be written back
 * to the user's config file.
 */

#undef CONFIGURATION_FIELD
#define CONFIGURATION_FIELD(T, NAME, FIELD)\
inline const T NAME##IgnoreOverride() const {\
	return _options.FIELD.value_or(Configuration::Defaults::FIELD);\
}\
inline const T NAME() const {\
	if (_overrides.FIELD.has_value()) {\
		return *_overrides.FIELD;\
	}\
	return NAME##IgnoreOverride();\
}\
inline void Set##NAME##Override(const std::optional<T> value) {\
	_overrides.FIELD = value;\
}\
inline void Set##NAME(const std::optional<T> value) {\
	if (NAME##HasOverride()) {\
		Set##NAME##Override(value);\
	} else {\
		_options.FIELD = value;\
	}\
}\
inline bool NAME##HasOverride() const {\
	return _overrides.FIELD.has_value();\
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

	inline bool IsBigPictureOrDeck() const {
		return SteamAPI_Init() && (SteamUtils()->IsSteamInBigPictureMode() || SteamUtils()->IsSteamRunningOnSteamDeck());
	}

	// Returns true if the launcher should attempt to launch Isaac immediately on init,
	// with a short countdown modal to give the user a chance to open the launcher window.
	bool AutoLaunch() const;

	// Same as AutoLaunch, except skipping the countdown part.
	bool AutoLaunchNoCountdown() const;

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
	CONFIGURATION_FIELD(bool, StealthMode, stealthMode);
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
	Options _overrides;
};

#undef CONFIGURATION_FIELD