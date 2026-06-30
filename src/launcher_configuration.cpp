#include <WinSock2.h>
#include <Windows.h>
#include <fstream>

#include "launcher/cli.h"

#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/utils.h"

#include "launcher/launcher_configuration.h"

using namespace Configuration;

static constexpr const char* launcherConfigFile = "repentogon_launcher.ini";
std::wstring LauncherConfiguration::_configurationPath;

template<typename T>
using ConfigurationTuple = std::tuple<std::string, std::string, T>;

static ConfigurationTuple<std::string> IsaacExecutablePathConf() {
	return { Sections::general, Keys::isaacExecutableKey, "" };
}

static ConfigurationTuple<bool> RanWizardConf() {
	return { Sections::general, Keys::ranWizard, Defaults::ranWizard };
}

static ConfigurationTuple<bool> HideWindowConf() {
	return { Sections::general, Keys::hideWindow, Defaults::hideWindow };
}

 static ConfigurationTuple<bool> StealthModeConf() {
 	return { Sections::general, Keys::stealthMode, Defaults::stealthMode };
 }

static ConfigurationTuple<bool> ConsoleConf() {
	return { Sections::repentogon, Keys::console, Defaults::console };
}

static ConfigurationTuple<int> StageConf() {
	return { Sections::vanilla, Keys::levelStage, Defaults::levelStage };
}

static ConfigurationTuple<bool> LuaDebugConf() {
	return { Sections::vanilla, Keys::luaDebug, Defaults::luaDebug };
}

static ConfigurationTuple<int> StageTypeConf() {
	return { Sections::vanilla, Keys::stageType, Defaults::stageType };
}

static ConfigurationTuple<std::string> LuaHeapSizeConf() {
	return { Sections::vanilla, Keys::luaHeapSize, Defaults::luaHeapSize };
}

static ConfigurationTuple<bool> AutomaticUpdatesConf() {
	return { Sections::repentogon, Keys::update, Defaults::automaticUpdates };
}

static ConfigurationTuple<bool> UnstableUpdatesConf() {
	return { Sections::repentogon, Keys::unstableUpdates, Defaults::unstableUpdates };
}

static ConfigurationTuple<int> LaunchModeConf() {
	return { Sections::shared, Keys::launchMode, Defaults::launchMode };
}

// static ConfigurationTuple<std::string> LoadRoomConf() {
// 	return { Sections::vanilla, Keys::loadRoom, Defaults::loadRoom };
// }

static std::string ReadString(INIReader const& reader, ConfigurationTuple<std::string>(*fn)()) {
	auto [section, key, def] = fn();
	return reader.Get(section, key, def);
}

static bool ReadBoolean(INIReader const& reader, ConfigurationTuple<bool>(*fn)()) {
	auto [section, key, def] = fn();
	return reader.GetBoolean(section, key, def);
}

static int ReadInteger(INIReader const& reader, ConfigurationTuple<int>(*fn)()) {
	auto [section, key, def] = fn();
	return reader.GetInteger(section, key, def);
}

bool LauncherConfiguration::InitializeConfigurationPath(std::optional<std::string> const& hint) {
	std::filesystem::path tentativePath;
	if (hint) {
		tentativePath = *hint;
	} else {
		std::filesystem::path path = utils::GetUserMyGamesDir();

		if (path.empty()) {
			Logger::Error("LauncherConfiguration::InitializeConfigurationPath: Unable to find user profile directory.\n");
			return false;
		}
		path /= launcherConfigFile;

		tentativePath = path;
	}

	FILE* file = _wfopen(tentativePath.wstring().c_str(), L"a");
	if (!file) {
		Logger::Error("LauncherConfiguration::InitializeConfigurationPath: Failed to open/initialize %s\n", tentativePath.u8string().c_str());
		return false;
	}
	fclose(file);

	_configurationPath = tentativePath.wstring();
	return true;
}

LauncherConfiguration::LauncherConfiguration() {

}

bool LauncherConfiguration::AutoLaunchNoCountdown() const {
	return sCLI->BasementRenovator();
}

bool LauncherConfiguration::AutoLaunch() const {
	return AutoLaunchNoCountdown() || StealthMode() || IsBigPictureOrDeck();
}

bool LauncherConfiguration::Load(LauncherConfigurationLoad* outResult) {
	if (!std::filesystem::exists(_configurationPath)) {
		// Fatal: the file should exist. Worst case scenario, it's empty.
		Logger::Fatal("Configuration file %s does not exist\n", GetConfigurationPathUTF8().c_str());
		std::terminate();
	}

	if (!Process(outResult)) {
		return false;
	}

	_isLoaded = true;
	return true;
}

bool LauncherConfiguration::Process(LauncherConfigurationLoad* outResult) {
	std::ifstream file(_configurationPath);
	std::string buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	INIReader reader(buffer.c_str(), buffer.size());
	if (int error = reader.ParseError(); error != 0) {
		switch (error) {
		case -1:
			Logger::Error("Unable to open launcher configuration file %s\n", GetConfigurationPathUTF8().c_str());

			if (outResult)
				*outResult = LAUNCHER_CONFIGURATION_LOAD_OPEN;
			return false;

		default:
			_configFileParseErrorLine = error;
			Logger::Error("INI parse error on line %d of configuration file %s\n", error, GetConfigurationPathUTF8().c_str());

			if (outResult)
				*outResult = LAUNCHER_CONFIGURATION_LOAD_PARSE_ERROR;
			return false;
		}
	}

	Logger::Info("Successfully opened and parsed launcher configuration file %s\n", GetConfigurationPathUTF8().c_str());
	Load(reader);

	if (IsaacExecutablePathIgnoreOverride().empty()) {
		if (outResult)
			*outResult = LAUNCHER_CONFIGURATION_LOAD_NO_ISAAC;
		return false;
	}

	return true;
}

void LauncherConfiguration::Load(INIReader const& reader) {
	LoadFromFile(reader);
	LoadFromCLI();
}

void LauncherConfiguration::LoadFromFile(INIReader const& reader) {
	_options.isaacExecutablePath = ReadString(reader, IsaacExecutablePathConf);
	_options.ranWizard = ReadBoolean(reader, RanWizardConf);

	_options.launchMode = (LaunchMode)ReadInteger(reader, LaunchModeConf);

	_options.levelStage = ReadInteger(reader, StageConf);
	_options.stageType = ReadInteger(reader, StageTypeConf);
	_options.luaDebug = ReadBoolean(reader, LuaDebugConf);
	_options.luaHeapSize = ReadString(reader, LuaHeapSizeConf);
	// Not reading LoadRoom from the config file yet as it is not supported in the UI.
	// _options.loadRoom = ReadString(reader, LoadRoomConf);
	_options.hideWindow = ReadBoolean(reader, HideWindowConf);
	 _options.stealthMode = ReadBoolean(reader, StealthModeConf);

	_options.console = ReadBoolean(reader, ConsoleConf);
	_options.automaticUpdates = ReadBoolean(reader, AutomaticUpdatesConf);
	_options.unstableUpdates = ReadBoolean(reader, UnstableUpdatesConf);
}

void LauncherConfiguration::LoadFromCLI() {
	std::string const& isaacPath = sCLI->IsaacPath();
	if (!isaacPath.empty()) {
		_overrides.isaacExecutablePath = isaacPath;
	}

	if (sCLI->SkipWizard()) {
		_overrides.ranWizard = true;
	} else if (sCLI->ForceWizard()) {
		_overrides.ranWizard = false;
	}

	_overrides.launchMode = sCLI->GetLaunchMode();

	if (sCLI->StealthMode()) {
		_overrides.stealthMode = true;
	}

	if (sCLI->Stage()) {
		_overrides.levelStage = sCLI->Stage();
		_overrides.stageType = sCLI->StageType();
	}
	if (sCLI->LuaDebug()) {
		_overrides.luaDebug = true;
	}
	if (!sCLI->LoadRoom().empty()) {
		_overrides.loadRoom = sCLI->LoadRoom();
	}
	if (!sCLI->LuaHeapSize().empty()) {
		_overrides.luaHeapSize = sCLI->LuaHeapSize();
	}

	if (sCLI->RepentogonConsole()) {
		_overrides.console = true;
	}
	if (sCLI->UnstableUpdates()) {
		_overrides.unstableUpdates = true;
	}
	if (sCLI->AutomaticUpdates()) {
		_overrides.automaticUpdates = true;
	}
}

void LauncherConfiguration::Write() {
	if (!std::filesystem::exists(_configurationPath)) {
		Logger::Error("No launcher configuration file found previously, cannot save settings\n");
		return;
	}

	FILE* f = _wfopen(_configurationPath.c_str(), L"w");
	if (!f) {
		Logger::Error("Unable to open launcher configuration file %s\n", GetConfigurationPathUTF8().c_str());
		return;
	}

	fprintf(f, "[%s]\n", Sections::general.c_str());
	fprintf(f, "%s = %s\n", Keys::isaacExecutableKey.c_str(), IsaacExecutablePathIgnoreOverride().c_str());
	fprintf(f, "%s = %d\n", Keys::ranWizard.c_str(), RanWizardIgnoreOverride() ? 1 : 0);
	fprintf(f, "%s = %d\n", Keys::hideWindow.c_str(), HideWindowIgnoreOverride() ? 1 : 0);
	fprintf(f, "%s = %d\n", Keys::stealthMode.c_str(), StealthModeIgnoreOverride() ? 1 : 0);

	fprintf(f, "[%s]\n", Sections::repentogon.c_str());
	fprintf(f, "%s = %d\n", Keys::console.c_str(), RepentogonConsoleIgnoreOverride());
	fprintf(f, "%s = %d\n", Keys::update.c_str(), AutomaticUpdatesIgnoreOverride());
	fprintf(f, "%s = %d\n", Keys::unstableUpdates.c_str(), UnstableUpdatesIgnoreOverride() ? 1 : 0);

	fprintf(f, "[%s]\n", Sections::vanilla.c_str());

	if (const long levelStage = StageIgnoreOverride(); levelStage > 0) {
		fprintf(f, "%s = %ld\n", Keys::levelStage.c_str(), levelStage);
		fprintf(f, "%s = %ld\n", Keys::stageType.c_str(), StageTypeIgnoreOverride());
	}

	if (const std::string& loadRoom = LoadRoomIgnoreOverride(); !loadRoom.empty()) {
		fprintf(f, "%s = %s\n", Keys::loadRoom.c_str(), loadRoom.c_str());
	}

	if (const std::string& luaHeapSize = LuaHeapSizeIgnoreOverride(); !luaHeapSize.empty()) {
		fprintf(f, "%s = %s\n", Keys::luaHeapSize.c_str(), luaHeapSize.c_str());
	}

	fprintf(f, "%s = %d\n", Keys::luaDebug.c_str(), LuaDebugIgnoreOverride());

	fprintf(f, "[%s]\n", Sections::shared.c_str());
	fprintf(f, "%s = %d\n", Keys::launchMode.c_str(), IsaacLaunchModeIgnoreOverride());

	fclose(f);
}