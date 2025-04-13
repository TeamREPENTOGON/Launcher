#include <WinSock2.h>
#include <Windows.h>

#include "shared/externals.h"
#include "shared/filesystem.h"
#include "shared/logger.h"

#include "launcher/configuration.h"
#include "launcher/launcher_configuration.h"

static constexpr const char* launcherConfigFile = "repentogon_launcher.ini";

LauncherConfiguration::LauncherConfiguration() {

}

bool LauncherConfiguration::Load(LauncherConfigurationLoad* outResult,
	std::optional<std::string> const& path) {
	if (!path) {
		if (!Search(outResult)) {
			return false;
		}
	} else {
		if (!CheckConfigurationFileExists(outResult, *path)) {
			Logger::Error("Configuration file %s does not exist\n", path->c_str());
			return false;
		}

		_configurationPath = *path;
	}

	if (!Process(outResult)) {
		return false;
	}

	_isLoaded = true;
	return true;
}

bool LauncherConfiguration::Search(LauncherConfigurationLoad* outResult) {
	char homeDirectory[4096];
	DWORD homeLen = 4096;
	HANDLE token = GetCurrentProcessToken();

	if (!Externals::pGetUserProfileDirectoryA) {
		DWORD result = GetEnvironmentVariableA("USERPROFILE", homeDirectory, 4096);
		if (result < 0) {
			Logger::Error("Installation::GetIsaacSaveFolder: no GetUserProfileDirectoryA and no %USERPROFILE\n");

			if (outResult)
				*outResult = LAUNCHER_CONFIGURATION_LOAD_NO_USERPROFILE;
			return false;
		}
	} else {
		BOOL result = Externals::pGetUserProfileDirectoryA(token, homeDirectory, &homeLen);

		if (!result) {
			Logger::Error("Installation::GetIsaacSaveFolder: unable to find user profile directory: %d\n", GetLastError());

			if (outResult)
				*outResult = LAUNCHER_CONFIGURATION_LOAD_GET_USER_DIRECTORY;
			return false;
		}
	}

	std::string path(homeDirectory);
	path += "\\Documents\\My Games\\";
	if (!Filesystem::FolderExists(path.c_str())) {
		if (!Filesystem::CreateFileHierarchy(path.c_str(), "\\")) {

			if (outResult)
				*outResult = LAUNCHER_CONFIGURATION_LOAD_HIERARCHY;
			return false;
		}
	}
	path += launcherConfigFile;

	if (!CheckConfigurationFileExists(outResult, path)) {
		return false;
	}

	_configurationPath = std::move(path);
	Logger::Info("Found launcher configuration file %s\n", _configurationPath.c_str());
	return true;
}

bool LauncherConfiguration::Process(LauncherConfigurationLoad* outResult) {
	std::unique_ptr<INIReader> reader(new INIReader(_configurationPath));
	if (int error = reader->ParseError(); error != 0) {
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
	_configurationFile = std::move(reader);
	return true;
}

bool LauncherConfiguration::CheckConfigurationFileExists(LauncherConfigurationLoad* result,
	std::string const& path) {
	if (!Filesystem::FileExists(path.c_str())) {
		Logger::Info("Launcher configuration file %s does not exist\n", path.c_str());

		if (result)
			*result = LAUNCHER_CONFIGURATION_LOAD_NOT_FOUND;
		return false;
	}

	return true;
}

std::string LauncherConfiguration::GetIsaacExecutablePath() {
	return _configurationFile->Get(Launcher::Configuration::GeneralSection,
		Launcher::Configuration::IsaacExecutableKey,
		Launcher::Configuration::EmptyPath);
}

bool LauncherConfiguration::HasAutomaticUpdates() const {
	if (_dirtyAutomaticUpdates) {
		return _overrideAutomaticUpdates;
	}

	auto [section, key, def] = Launcher::Configuration::HasAutomaticUpdates();
	return _configurationFile->GetBoolean(section, key, def);
}

bool LauncherConfiguration::HasUnstableUpdates() const {
	if (_dirtyUnstableUpdates) {
		return _overrideUnstableUpdates;
	}

	auto [section, key, def] = Launcher::Configuration::HasUnstableUpdates();
	return _configurationFile->GetBoolean(section, key, def);
}

void LauncherConfiguration::OverrideAutomaticUpdates(bool value) {
	_dirtyAutomaticUpdates = true;
	_overrideAutomaticUpdates = value;
}

void LauncherConfiguration::OverrideUnstableUpdates(bool value) {
	_dirtyUnstableUpdates = true;
	_overrideUnstableUpdates = value;
}

void LauncherConfiguration::Invalidate() {
	_dirtyAutomaticUpdates = _dirtyUnstableUpdates = false;
	_isLoaded = Process(nullptr);
}