#include <Windows.h>
#include <bcrypt.h>
#include <Psapi.h>
#include <UserEnv.h>

#include <functional>
#include <sstream>
#include <stdexcept>

#include <cwchar>

#include "inih/cpp/INIReader.h"
#include "launcher/externals.h"
#include "launcher/filesystem.h"
#include "shared/logger.h"
#include "shared/filesystem.h"
#include "shared/sha256.h"

namespace Launcher::fs {
	/* Array of all the names of files that must be found for the installation
	 * to be considered a valid Repentogon installation.
	 */
	static const char* mandatoryFileNames[] = {
		"libzhl.dll",
		"repentogonLauncher.dll",
		"zhlRepentogon.dll",
		"freetype.dll",
		NULL
	};

	Version const knownVersions[] = {
		{ "04469d0c3d3581936fcf85bea5f9f4f3a65b2ccf96b36310456c9626bac36dc6", "v1.7.9b.J835 (Steam)", true },
		{ "d00523d04dd43a72071f1d936e5ff34892da20800d32f05a143eba0a4fe29361", "Dummy (Test Suite)", true },
		{ NULL, NULL, false }
	};

	/* RAII-style class that automatically unloads a module upon destruction. */
	class ScoppedModule {
	public:
		ScoppedModule(HMODULE mod) : _module(mod) {

		}

		~ScoppedModule() {
			if (_module) {
				FreeLibrary(_module);
			}
		}

		HMODULE Get() const {
			return _module;
		}

	private:
		HMODULE _module = NULL;
	};

	Version const* GetIsaacVersionFromHash(const char* hash) {
		Version const* version = knownVersions;
		while (version->version) {
			if (!strcmp(hash, version->hash)) {
				return version;
			}

			++version;
		}

		return NULL;
	}

	IsaacInstallationPathInitResult Installation::InitFolders() {
		IsaacInstallationPathInitResult result = SearchSaveFolder();
		IsaacInstallationPathInitResult saveResult = result;
		if (result != INSTALLATION_PATH_INIT_OK) {
			Logger::Warn("Error while lokking for save folder: %d\n", result);
		}
			
		result = SearchConfigurationFile();
		if (result != INSTALLATION_PATH_INIT_OK) {
			if (result == INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR) {
				return saveResult;
			}
			return result;
		}

		result = SearchIsaacInstallationPath();
		if (result != INSTALLATION_PATH_INIT_OK) {
			return result;
		}

		result = SearchRepentogonInstallationPath();
		return result;
	}

	bool Installation::CheckIsaacInstallation() {
		if (_isaacFolder.empty()) {
			return false;
		}

		std::string fullPath = (_isaacFolder + "\\") + isaacExecutable;
		if (!Filesystem::FileExists(fullPath.c_str())) {
			return false;
		}

		try {
			std::string hash = Sha256::Sha256F(fullPath.c_str());
			_isaacVersion = GetIsaacVersionFromHash(hash.c_str());
		}
		catch (std::runtime_error& e) {
			Logger::Error("Updater::CheckIsaacInstallation: exception: %s\n", e.what());
		}

		return true;
	}

	bool Installation::SetIsaacInstallationFolder(std::string const& path) {
		std::string backup = _isaacFolder;
		_isaacFolder = path;

		if (!CheckIsaacInstallation()) {
			_isaacFolder = backup;
			return false;
		}

		return true;
	}

	bool Installation::CheckRepentogonInstallation() {
		if (_repentogonFolder.empty()) {
			return false;
		}

		_repentogonFiles.clear();

		const char** mandatoryFile = mandatoryFileNames;
		bool ok = true;
		while (*mandatoryFile) {
			std::string fullName = _repentogonFolder + *mandatoryFile;
			FoundFile& file = _repentogonFiles.emplace_back();
			file.filename = *mandatoryFile;
			file.found = Filesystem::FileExists(fullName.c_str());

			if (!file.found) {
				Logger::Error("Updater::CheckRepentogonInstallation: %s not found\n", *mandatoryFile);
			}

			ok = ok && file.found;
			++mandatoryFile;
		}

		if (!ok) {
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		ScoppedModule zhl(LoadLib((_repentogonFolder + "libzhl.dll").c_str(), LOADABLE_DLL_ZHL));
		if (!zhl.Get()) {
			Logger::Error("Updater::CheckRepentogonInstallation: Failed to load libzhl.dll (%d)\n", GetLastError());
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		Logger::Info("Updater::CheckRepentogonInstallation: loaded libzhl.dll\n");

		ScoppedModule repentogon(LoadLib((_repentogonFolder + "zhlRepentogon.dll").c_str(), LOADABLE_DLL_REPENTOGON));
		if (!repentogon.Get()) {
			Logger::Error("Updater::CheckRepentogonInstallation: Failed to load zhlRepentogon.dll (%d)\n", GetLastError());
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		Logger::Info("Updater::CheckRepentogonInstallation: loaded zhlRepentogon.dll\n");

		FARPROC zhlVersion = GetProcAddress(zhl.Get(), "__ZHL_VERSION");
		if (!zhlVersion) {
			Logger::Warn("Updater::CheckRepentogonInstallation: libzhl.dll does not export __ZHL_VERSION\n");
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		Logger::Info("Updater::CheckRepentogonInstallation: found __ZHL_VERSION at %p\n", zhlVersion);

		FARPROC repentogonVersion = GetProcAddress(repentogon.Get(), "__REPENTOGON_VERSION");
		if (!repentogonVersion) {
			Logger::Warn("Updater::CheckRepentogonInstallation: zhlRepentogon.dll does not export __REPENTOGON_VERSION\n");
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		Logger::Info("Updater::CheckRepentogonInstallation: found __REPENTOGON_VERSION at %p\n", repentogonVersion);

		const char** zhlVersionStr = (const char**)zhlVersion;
		const char** repentogonVersionStr = (const char**)repentogonVersion;

		if (!ValidateVersionString(zhl.Get(), zhlVersionStr, _zhlVersion)) {
			Logger::Error("Updater::CheckRepentogonInstallation: malformed libzhl.dll, __ZHL_VERSION extends past the module's boundaries\n");
			MessageBoxA(NULL, "Alert",
				"Your build of libzhl.dll is malformed\n"
				"If you downloaded ZHL and/or Repentogon without using the launcher, the legacy updater or without going through GitHub, you may be exposing yourself to risks\n"
				"As a precaution, the launcher will not allow you to start Repentogon until you download a clean copy\n"
				"You may be prompted to download the latest release",
				MB_ICONEXCLAMATION);
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		Logger::Info("Updater::CheckRepentogonInstallation: identified ZHL version %s\n", *zhlVersionStr);

		if (!ValidateVersionString(repentogon.Get(), repentogonVersionStr, _repentogonVersion)) {
			Logger::Error("Updater::CheckRepentogonInstallation: malformed zhlRepentogon.dll, __ZHL_VERSION extends past the module's boundaries\n");
			MessageBoxA(NULL, "Alert",
				"Your build of zhlRepentogon.dll is malformed\n"
				"If you downloaded ZHL and/or Repentogon without using the launcher, the legacy updater or without going through GitHub, you may be exposing yourself to risks\n"
				"As a precaution, the launcher will not allow you to start Repentogon until you download a clean copy\n"
				"You may be prompted to download the latest release",
				MB_ICONEXCLAMATION);
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		Logger::Info("Updater::CheckRepentogonInstallation: identified Repentogon version %s\n", *repentogonVersionStr);

		_zhlVersion = *zhlVersionStr;
		_repentogonVersion = *repentogonVersionStr;

		if (_zhlVersion != _repentogonVersion) {
			Logger::Warn("Updater::CheckRepentogonInstallation: ZHL / Repentogon version mismatch (ZHL version %s, Repentogon version %s)\n",
				_zhlVersion.c_str(), _repentogonVersion.c_str());
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			_repentogonZHLVersionMatch = false;
			return false;
		}

		_repentogonZHLVersionMatch = true;

		if (Filesystem::FileExists((_repentogonFolder + "dsound.dll").c_str())) {
			_installationState = REPENTOGON_INSTALLATION_STATE_LEGACY;
		}
		else {
			_installationState = REPENTOGON_INSTALLATION_STATE_MODERN;
		}

		return true;
	}

	void Installation::SetLauncherConfigurationPath(ConfigurationFileLocation loc) {
		if (loc == CONFIG_FILE_LOCATION_HERE) {
			_configurationPath = Filesystem::GetCurrentDirectory_();
		}
		else {
			if (_saveFolder.empty()) {
				Logger::Error("Trying to set configuration file location to save folder when no save folder was found\n");
				return;
			}

			_configurationPath = (_saveFolder + "\\") + launcherConfigFile;
		}
	}

	HMODULE Installation::LoadLib(const char* name, LoadableDlls dll) {
		/* Library must be loaded using IMAGE_RESOURCE in order to sanitize it
		 * using GetModuleInformation() later.
		 */
		HMODULE lib = LoadLibraryExA(name, NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (!lib) {
			return NULL;
		}

		_dllStates[dll] = true;
		return lib;
	}

	bool ValidateVersionString(HMODULE module, const char** addr, std::string& version) {
		MODULEINFO info;
		DWORD result = GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info));
		if (result == 0) {
			Logger::Error("Updater::ValidateVersionString: "
				"Unable to retrieve information about the module (%d)\n", GetLastError());
			return false;
		}

		/* Start walking the DLL. The walk ends when we find a 0, or when we
		 * would exit the boundaries.
		 */
		const char* base = (const char*)addr;
		const char* limit = (const char*)info.lpBaseOfDll + info.SizeOfImage;

		while (base < limit && *base) {
			++base;
		}

		/* base == limit. The PE format makes it virtually impossible for the
		 * string to end on the very last byte.
		 */
		if (*base) {
			Logger::Error("Updater::ValidateVersionString: string extends past the boundaries of the module\n");
			return false;
		}

		version = *addr;
		return true;
	}

	RepentogonInstallationState Installation::GetRepentogonInstallationState() const {
		return _installationState;
	}

	Version const* Installation::GetIsaacVersion() const {
		return _isaacVersion;
	}

	bool Installation::WasLibraryLoaded(LoadableDlls dll) const {
		return _dllStates[dll];
	}

	std::vector<FoundFile> const& Installation::GetRepentogonInstallationFilesState() const {
		return _repentogonFiles;
	}

	std::string const& Installation::GetRepentogonVersion() const {
		return _repentogonVersion;
	}

	std::string const& Installation::GetZHLVersion() const {
		return _zhlVersion;
	}

	bool Installation::RepentogonZHLVersionMatch() const {
		return _repentogonZHLVersionMatch;
	}

	std::string const& Installation::GetSaveFolder() const {
		return _saveFolder;
	}

	std::string const& Installation::GetLauncherConfigurationPath() const {
		return _configurationPath;
	}

	std::string const& Installation::GetIsaacInstallationFolder() const {
		return _isaacFolder;
	}

	std::string const& Installation::GetRepentogonInstallationFolder() const {
		return _repentogonFolder;
	}

	int Installation::GetConfigurationFileSyntaxErrorLine() const {
		return _configFileParseErrorLine;
	}

	IsaacInstallationPathInitResult Installation::ProcessConfigurationFile(std::string const& path) {
		_configurationFile.reset(new INIReader(path));
		if (int error = _configurationFile->ParseError(); error != 0) {
			switch (error) {
			case -1:
				Logger::Error("Unable to open launcher configuration file %s\n", path.c_str());
				return INSTALLATION_PATH_INIT_ERR_OPEN_CONFIG;

			default:
				_configFileParseErrorLine = error;
				Logger::Error("INI parse error on line %d of configuration file %s\n", error, path.c_str());
				return INSTALLATION_PATH_INIT_ERR_PARSE_CONFIG;
			}
		}

		Logger::Info("Successfully opened and parsed launcher configuration file %s\n", path.c_str());
		return INSTALLATION_PATH_INIT_OK;
	}

	IsaacInstallationPathInitResult Installation::SearchSaveFolder() {
		char homeDirectory[4096];
		DWORD homeLen = 4096;
		HANDLE token = GetCurrentProcessToken();

		if (!Externals::pGetUserProfileDirectoryA) {
			DWORD result = GetEnvironmentVariableA("USERPROFILE", homeDirectory, 4096);
			if (result < 0) {
				return INSTALLATION_PATH_INIT_ERR_PROFILE_DIR;
			}
		}
		else {
			BOOL result = Externals::pGetUserProfileDirectoryA(token, homeDirectory, &homeLen);

			if (!result) {
				Logger::Error("Unable to find user profile directory: %d\n", GetLastError());
				return INSTALLATION_PATH_INIT_ERR_PROFILE_DIR;
			}
		}

		std::string path(homeDirectory);
		path += "\\Documents\\My Games\\Binding of Isaac Repentance";

		if (!Filesystem::FolderExists(path.c_str())) {
			Logger::Error("Repentance save folder %s does not exist\n", path.c_str());
			return INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR;
		}

		_saveFolder = std::move(path);
		return INSTALLATION_PATH_INIT_OK;
	}

	IsaacInstallationPathInitResult Installation::SearchConfigurationFile() {
		if (Filesystem::FileExists(launcherConfigFile)) {
			std::string path = (Filesystem::GetCurrentDirectory_() + "\\" + launcherConfigFile);
			IsaacInstallationPathInitResult parseRes = ProcessConfigurationFile(path.c_str());
			if (parseRes != INSTALLATION_PATH_INIT_OK) {
				Logger::Error("Error while opening/parsing the launcher configuration file\n");
				return parseRes;
			}

			_configurationPath = std::move(path);
			Logger::Info("Found configuration file %s\n", _configurationPath.c_str());
			return parseRes;
		}
		else {
			std::string path = _saveFolder;
			if (path.empty()) {
				return INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR;
			}

			path += "\\";
			path += launcherConfigFile;

			if (!Filesystem::FileExists(path.c_str())) {
				Logger::Error("No launcher configuration file found in Repentance save folder\n");
				return INSTALLATION_PATH_INIT_ERR_NO_CONFIG;
			}

			IsaacInstallationPathInitResult parseRes = ProcessConfigurationFile(path);
			if (parseRes != INSTALLATION_PATH_INIT_OK) {
				Logger::Error("Error while opening/parsing the launcher configuration file\n");
				return parseRes;
			}

			_configurationPath = std::move(path);
			Logger::Info("Found configuration file %s\n", _configurationPath.c_str());
			return parseRes;
		}
	}

	IsaacInstallationPathInitResult Installation::SearchIsaacInstallationPath() {
		std::string isaacFolder = _configurationFile->GetString("General", "IsaacFolder", "__empty__");
		if (isaacFolder == "__empty__") {
			if (Filesystem::FileExists(isaacExecutable)) {
				_isaacFolder = Filesystem::GetCurrentDirectory_();
				Logger::Info("Found Isaac installation in %s\n", _isaacFolder.c_str());
				return INSTALLATION_PATH_INIT_OK;
			}

			Logger::Error("Configuration file has no Isaac installation folder and no isaac-ng.exe found in current folder\n");
			return INSTALLATION_PATH_INIT_ERR_NO_ISAAC;
		}

		if (!Filesystem::FolderExists(isaacFolder.c_str())) {
			Logger::Error("Isaac installation folder %s in launcher configuration file does not exist\n", isaacFolder.c_str());
			return INSTALLATION_PATH_INIT_ERR_BAD_ISAAC_FOLDER;
		}

		_isaacFolder = std::move(isaacFolder);
		Logger::Info("Found Isaac installation in %s\n", _isaacFolder.c_str());
		return INSTALLATION_PATH_INIT_OK;
	}

	IsaacInstallationPathInitResult Installation::SearchRepentogonInstallationPath() {
		std::string repentogonFolder = _configurationFile->GetString("General", "RepentogonFolder", "__empty__");
		if (repentogonFolder == "__empty__") {
			if (Filesystem::FolderExists(defaultRepentogonFolder)) {
				_repentogonFolder = (Filesystem::GetCurrentDirectory_() + "\\") + defaultRepentogonFolder;
				Logger::Info("Found Repentogon installation folder in %s\n", _repentogonFolder.c_str());
			}
			else {
				Logger::Info("No Repentogon installation folder in launcher configuration file\n");
			}

			/* In this context it is not an error to not find a folder. */
			return INSTALLATION_PATH_INIT_OK;
		}

		if (!Filesystem::FolderExists(repentogonFolder.c_str())) {
			Logger::Error("Repentogon installation folder %s in launcher configuration file does not exist\n", repentogonFolder.c_str());
			return INSTALLATION_PATH_INIT_ERR_BAD_REPENTOGON_FOLDER;
		}

		_repentogonFolder = std::move(repentogonFolder);
		Logger::Info("Found Repentogon installation folder %s\n", _repentogonFolder.c_str());
		return INSTALLATION_PATH_INIT_OK;
	}
}