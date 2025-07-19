#include <WinSock2.h>
#include <Windows.h>

#include "launcher/cli.h"

#include "shared/externals.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

#include "launcher/launcher_configuration.h"

using namespace Configuration;

static constexpr const char* launcherConfigFile = "repentogon_launcher.ini";
std::string LauncherConfiguration::_configurationPath;

template<typename T>
using ConfigurationTuple = std::tuple<std::string, std::string, T>;

static ConfigurationTuple<std::string> GetIsaacExecutablePath() {
	return { Sections::general, Keys::isaacExecutableKey, "" };
}

static ConfigurationTuple<bool> HasRanWizard() {
	return { Sections::general, Keys::ranWizard, Defaults::ranWizard };
}

static ConfigurationTuple<bool> HasHideWindow() {
	return { Sections::general, Keys::hideWindow, Defaults::hideWindow };
}

static ConfigurationTuple<bool> HasConsole() {
	return { Sections::repentogon, Keys::console, Defaults::console };
}

static ConfigurationTuple<int> Stage() {
	return { Sections::vanilla, Keys::levelStage, Defaults::levelStage };
}

static ConfigurationTuple<bool> HasLuaDebug() {
	return { Sections::vanilla, Keys::luaDebug, Defaults::luaDebug };
}

static ConfigurationTuple<int> StageType() {
	return { Sections::vanilla, Keys::stageType, Defaults::stageType };
}

static ConfigurationTuple<std::string> LuaHeapSize() {
	return { Sections::vanilla, Keys::luaHeapSize, Defaults::luaHeapSize };
}

static ConfigurationTuple<bool> HasAutomaticUpdates() {
	return { Sections::repentogon, Keys::update, Defaults::update };
}

static ConfigurationTuple<bool> HasUnstableUpdates() {
	return { Sections::repentogon, Keys::unstableUpdates, Defaults::unstableUpdates };
}

static ConfigurationTuple<int> GetLaunchMode() {
	return { Sections::shared, Keys::launchMode, LAUNCH_MODE_REPENTOGON };
}

static ConfigurationTuple<int> RoomId() {
	return { Sections::vanilla, Keys::roomId, 0 };
}

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

bool LauncherConfiguration::InitializeConfigurationPath(
	LauncherConfigurationInitialize* outResult, std::optional<std::string> const& hint) {
	std::string tentativePath;
	if (hint) {
		tentativePath = *hint;
	} else {
		char homeDirectory[4096];
		DWORD homeLen = 4096;
		HANDLE token = GetCurrentProcessToken();

		if (!Externals::pGetUserProfileDirectoryA) {
			DWORD result = GetEnvironmentVariableA("USERPROFILE", homeDirectory, 4096);
			if (result < 0) {
				Logger::Error("Installation::GetIsaacSaveFolder: no GetUserProfileDirectoryA and no %USERPROFILE\n");

				if (outResult)
					*outResult = LAUNCHER_CONFIGURATION_INIT_NO_USERPROFILE;
				return false;
			}
		} else {
			BOOL result = Externals::pGetUserProfileDirectoryA(token, homeDirectory, &homeLen);

			if (!result) {
				Logger::Error("Installation::GetIsaacSaveFolder: unable to find user profile directory: %d\n", GetLastError());

				if (outResult)
					*outResult = LAUNCHER_CONFIGURATION_INIT_GET_USER_DIRECTORY;
				return false;
			}
		}

		std::string path(homeDirectory);
		path += "\\Documents\\My Games\\";
		if (!Filesystem::IsFolder(path.c_str())) {
			if (!Filesystem::CreateFileHierarchy(path.c_str(), "\\")) {

				if (outResult)
					*outResult = LAUNCHER_CONFIGURATION_INIT_HIERARCHY;
				return false;
			}
		}
		path += launcherConfigFile;

		tentativePath = std::move(path);
	}

	FILE* file = fopen(tentativePath.c_str(), "a");
	if (!file) {
		if (outResult)
			*outResult = LAUNCHER_CONFIGURATION_INIT_INVALID_PATH;
		return false;
	}

	fclose(file);

	_configurationPath = std::move(tentativePath);
	return true;
}

LauncherConfiguration::LauncherConfiguration() {

}

bool LauncherConfiguration::Load(LauncherConfigurationLoad* outResult) {
	if (_configurationPath.empty()) {
		Logger::Fatal("Attempting to load configuration when none was found");
		std::terminate();
	}

	if (!CheckConfigurationFileExists()) {
		// Fatal: the file should exist. Worst case scenario, it's empty.
		Logger::Fatal("Configuration file %s does not exist\n", _configurationPath.c_str());
		std::terminate();
	}

	if (!Process(outResult)) {
		return false;
	}

	_isLoaded = true;
	return true;
}

bool LauncherConfiguration::Process(LauncherConfigurationLoad* outResult) {
	INIReader reader(_configurationPath);
	if (int error = reader.ParseError(); error != 0) {
		switch (error) {
		case -1:
			Logger::Error("Unable to open launcher configuration file %s\n", _configurationPath.c_str());

			if (outResult)
				*outResult = LAUNCHER_CONFIGURATION_LOAD_OPEN;
			return false;

		default:
			_configFileParseErrorLine = error;
			Logger::Error("INI parse error on line %d of configuration file %s\n", error, _configurationPath.c_str());

			if (outResult)
				*outResult = LAUNCHER_CONFIGURATION_LOAD_PARSE_ERROR;
			return false;
		}
	}

	Logger::Info("Successfully opened and parsed launcher configuration file %s\n", _configurationPath.c_str());
	Load(reader);

	if (_isaacExecutablePath.empty()) {
		if (outResult)
			*outResult = LAUNCHER_CONFIGURATION_LOAD_NO_ISAAC;
		return false;
	}

	return true;
}

bool LauncherConfiguration::CheckConfigurationFileExists() {
	const char* path = GetConfigurationPath();
	if (!Filesystem::Exists(path)) {
		Logger::Info("Launcher configuration file %s does not exist\n", path);
		return false;
	}

	return true;
}

void LauncherConfiguration::Load(INIReader const& reader) {
	LoadFromFile(reader);
	LoadFromCLI();
}

void LauncherConfiguration::LoadFromFile(INIReader const& reader) {
	_isaacExecutablePath = ReadString(reader, GetIsaacExecutablePath);
	_ranWizard = ReadBoolean(reader, HasRanWizard);

	_launchMode = (LaunchMode)ReadInteger(reader, GetLaunchMode);

	_stage = ReadInteger(reader, ::Stage);
	_stageType = ReadInteger(reader, ::StageType);
	_luaDebug = ReadBoolean(reader, HasLuaDebug);
	_luaHeapSize = ReadString(reader, ::LuaHeapSize);
	_roomId = ReadInteger(reader, RoomId);
	_hideWindow = ReadBoolean(reader, HasHideWindow);

	_repentogonConsole = ReadBoolean(reader, HasConsole);
	_unstableUpdates = ReadBoolean(reader, HasUnstableUpdates);
	_update = ReadBoolean(reader, HasAutomaticUpdates);
}

void LauncherConfiguration::LoadFromCLI() {
	std::string const& isaacPath = sCLI->IsaacPath();
	if (!isaacPath.empty()) {
		_isaacExecutablePath = isaacPath;
	}

	if (sCLI->SkipWizard()) {
		_ranWizard = true;
	} else if (sCLI->ForceWizard()) {
		_ranWizard = false;
	}

	_launchMode = sCLI->GetLaunchMode();

	_stage = sCLI->Stage();
	_stageType = sCLI->StageType();
	if (sCLI->LuaDebug()) {
		_luaDebug = true;
	}
	_roomId = sCLI->LoadRoom();
	_luaHeapSize = sCLI->LuaHeapSize();

	if (sCLI->RepentogonConsole()) {
		_repentogonConsole = true;
	}

	if (sCLI->UnstableUpdates()) {
		_unstableUpdates = true;
	}

	if (sCLI->AutomaticUpdates()) {
		_update = true;
	}
}

void LauncherConfiguration::Write() {
	if (_configurationPath.empty()) {
		// gui->LogError("No launcher configuration file found previously, cannot save settings\n");
		Logger::Error("No launcher configuration file found previously, cannot save settings\n");
		return;
	}

	FILE* f = fopen(_configurationPath.c_str(), "w");
	if (!f) {
		// gui->LogError("Unable to open launcher configuration file %s\n", _configurationPath.c_str());
		Logger::Error("Unable to open launcher configuration file %s\n", _configurationPath.c_str());
		return;
	}

	fprintf(f, "[%s]\n", Sections::general.c_str());
	fprintf(f, "%s = %s\n", Keys::isaacExecutableKey.c_str(),
		_isaacExecutablePath.empty() ? "" : _isaacExecutablePath.c_str());
	fprintf(f, "%s = %d\n", Keys::ranWizard.c_str(),
		_ranWizard ? 1 : 0);
	fprintf(f, "%s = %d\n", Keys::hideWindow.c_str(), _hideWindow ? 1 : 0);

	fprintf(f, "[%s]\n", Sections::repentogon.c_str());
	fprintf(f, "%s = %d\n", Keys::console.c_str(), _repentogonConsole);
	fprintf(f, "%s = %d\n", Keys::update.c_str(), _update);
	fprintf(f, "%s = %d\n", Keys::unstableUpdates.c_str(), _unstableUpdates ? 1 : 0);

	fprintf(f, "[%s]\n", Sections::vanilla.c_str());

	if (_stage) {
		fprintf(f, "%s = %ld\n", Keys::levelStage.c_str(), *_stage);
	}

	if (_stageType) {
		fprintf(f, "%s = %ld\n", Keys::stageType.c_str(), *_stageType);
	}

	if (_luaHeapSize && !_luaHeapSize->empty()) {
		fprintf(f, "%s = %s\n", Keys::luaHeapSize.c_str(), _luaHeapSize->c_str());
	}

	fprintf(f, "%s = %d\n", Keys::luaDebug.c_str(), _luaDebug);

	fprintf(f, "[%s]\n", Sections::shared.c_str());
	fprintf(f, "%s = %d\n", Keys::launchMode.c_str(), _launchMode);

	fclose(f);
}