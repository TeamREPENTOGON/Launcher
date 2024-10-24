#include <WinSock2.h>
#include <Windows.h>
#include <bcrypt.h>
#include <Psapi.h>
#include <UserEnv.h>

#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>

#include <cwchar>

#include "inih/cpp/INIReader.h"
#include "launcher/filesystem.h"
#include "shared/externals.h"
#include "shared/logger.h"
#include "shared/filesystem.h"
#include "shared/sha256.h"

namespace Launcher::fs {
	/* Array of all the names of files that must be found for the installation
	 * to be considered a valid Repentogon installation.
	 */
	static const char* mandatoryFileNames[] = {
		Libraries::zhl,
		Libraries::repentogon,
		Libraries::loader,
		"freetype.dll",
		NULL
	};

	Version const knownVersions[] = {
		{ "04469d0c3d3581936fcf85bea5f9f4f3a65b2ccf96b36310456c9626bac36dc6", "v1.7.9b.J835 (Steam)", true },
		{ "d00523d04dd43a72071f1d936e5ff34892da20800d32f05a143eba0a4fe29361", "Dummy (Test Suite)", true },
		{ "31846486979cfa07c8c968221553052c3ff603518681ca11912d445f96ca9404", "v1.7.9b.J835 (Steamless, no binding section)", true },
		{ "530b4d2accef833b8dbf6f1a9f781fe1e051c1916ca5acdb579df05a34640627", "v1.7.9b.J835 (Steamless, with binding section)", true },
		{ NULL, NULL, false }
	};

	Version const* GetIsaacVersionFromHash(const char* hash) {
		Version const* version = knownVersions;
		while (version->version) {
			if (Sha256::Equals(hash, version->hash)) {
				Logger::Info("GetIsaacVersionFromHash: source hash %s equals version hash %s (%s)\n", hash, version->hash, version->version);
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
		Logger::Info("Checking Isaac installation...\n");
		if (_isaacFolder.empty()) {
			Logger::Info("No Isaac folder found\n");
			return false;
		}

		std::string fullPath = (_isaacFolder + "\\") + isaacExecutable;
		if (!Filesystem::FileExists(fullPath.c_str())) {
			Logger::Error("No isaac-ng.exe file in %s\n", fullPath.c_str());
			return false;
		}

		try {
			std::string hash = Sha256::Sha256F(fullPath.c_str());
			_isaacVersion = GetIsaacVersionFromHash(hash.c_str());
			Logger::Info("Computed hash of isaac-ng.exe: %s\n", hash.c_str());
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

	ScopedModule Installation::LoadModule(const char* shortName, const char* path, LoadableDlls dll) {
		HMODULE lib = LoadLib(path, dll);
		ScopedModule module(lib);
		if (!module) {
			Logger::Error("Installation::LoadModule: Failed to load %s (%d)\n", shortName, GetLastError());
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
		} else {
			Logger::Info("Installation::LoadModule: Loaded %s\n", shortName);
		}

		return module;
	}

	FARPROC Installation::RetrieveSymbol(HMODULE module, const char* libname, const char* symbol) {
		FARPROC result = GetProcAddress(module, symbol);
		if (!result) {
			Logger::Warn("Installation::RetrieveSymbol: %s does not export %s\n", libname, symbol);
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
		} else {
			Logger::Info("Installation::RetrieveSymbol: found %s at %p\n", symbol, result);
		}

		return result;
	}

	bool Installation::ValidateVersionSymbol(HMODULE module, const char* libname, const char* symbolName, 
		FARPROC symbol, std::string& target) {
		const char** version = (const char**)symbol;
		if (version) {
			if (!ValidateVersionString(module, version, target)) {
				char buffer[4096];
				Logger::Error("Installation::ValidateVersionSymbol: malformed %s, %s extends past the module's boundaries\n", libname, symbolName);
				snprintf(buffer, 4096, "Your build of %s is malformed\n"
					"If you downloaded ZHL and/or Repentogon without using either the launcher, the legacy updater or without going through Github "
					"you may be exposing yourself to risks\n"
					"As a precaution, the launcher will not allow you to start Repentogon until you download a clean copy\n"
					"Depending on your exact configuration, the launcher may prompt you to download the latest Repentogon release automatically.",
					libname);
				MessageBoxA(NULL, "Alert",
					buffer,
					MB_ICONEXCLAMATION);
				_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
				return false;
			} else {
				Logger::Info("Identified %s version %s\n", libname, target.c_str());
			}
		}

		return true;
	}

	bool Installation::CheckRepentogonInstallation() {
		std::string repentogonFolder = _repentogonFolder;

		if (_repentogonFolder.empty()) {
			// Assume legacy installation.
			if (_isaacFolder.empty()) {
				Logger::Error("Repentogon folder not specified in configuration file or configuration file absent, and no Isaac folder specified: assuming no Repentogon installation\n");
				return false;
			}

			repentogonFolder = _isaacFolder;
			Logger::Warn("Repentogon folder not specified in configuration file or configuration file absent: assuming legacy Repentogon installation\n");
		} else {
			if (_isaacFolder.empty()) {
				Logger::Fatal("Repentogon folder found (%s), but no Isaac folder specified, invalid state\n", _repentogonFolder.c_str());
				throw std::runtime_error("Invalid launcher state: cannot have a Repentogon folder and no Isaac folder");
				return false;
			}
		}

		_repentogonFiles.clear();

		const char** mandatoryFile = mandatoryFileNames;
		bool ok = true;
		while (*mandatoryFile) {
			std::string fullName = repentogonFolder + *mandatoryFile;
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

		ScopedModule loader = LoadModule(Libraries::loader, (repentogonFolder + Libraries::loader).c_str(), LOADABLE_DLL_ZHL_LOADER);
		if (!loader) {
			return false;
		}

		ScopedModule zhl = LoadModule(Libraries::zhl, (repentogonFolder + Libraries::zhl).c_str(), LOADABLE_DLL_LIBZHL);
		if (!zhl) {
			return false;
		}

		ScopedModule repentogon = LoadModule(Libraries::repentogon, (repentogonFolder + Libraries::repentogon).c_str(), LOADABLE_DLL_REPENTOGON);
		if (!repentogon) {
			return false;
		}

		FARPROC zhlVersion			= RetrieveSymbol(zhl,			Libraries::zhl,			Symbols::zhlVersion);
		FARPROC repentogonVersion	= RetrieveSymbol(repentogon,	Libraries::repentogon,	Symbols::repentogonVersion);
		FARPROC loaderVersion		= RetrieveSymbol(loader,		Libraries::loader,		Symbols::loaderVersion);

		if (!zhlVersion || !repentogonVersion || !loaderVersion) {
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		if (!ValidateVersionSymbol(loader, Libraries::loader, Symbols::loaderVersion, loaderVersion, _zhlLoaderVersion)) {
			return false;
		}

		if (!ValidateVersionSymbol(zhl, Libraries::zhl, Symbols::zhlVersion, zhlVersion, _zhlVersion)) {
			return false;
		}

		if (!ValidateVersionSymbol(repentogon, Libraries::repentogon, Symbols::repentogonVersion, repentogonVersion, _repentogonVersion)) {
			return false;
		}

		if (_zhlVersion != _repentogonVersion || _repentogonVersion != _zhlLoaderVersion) {
			Logger::Warn("Updater::CheckRepentogonInstallation: ZHL / Repentogon version mismatch (ZHL version %s, ZHL loader version %s, Repentogon version %s)\n",
				_zhlVersion.c_str(), _zhlLoaderVersion.c_str(), _repentogonVersion.c_str());
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			_repentogonZHLVersionMatch = false;
			return false;
		}

		_repentogonZHLVersionMatch = true;

		if (Filesystem::FileExists((_isaacFolder + "dsound.dll").c_str())) {
			_installationState = REPENTOGON_INSTALLATION_STATE_LEGACY;
		} else {
			_installationState = REPENTOGON_INSTALLATION_STATE_MODERN;
		}

		return true;
	}

	void Installation::SetLauncherConfigurationPath(ConfigurationFileLocation loc) {
		if (loc == CONFIG_FILE_LOCATION_HERE) {
			_configurationPath = Filesystem::GetCurrentDirectory_();
		} else {
			if (_saveFolder.empty()) {
				Logger::Error("Trying to set configuration file location to save folder when no save folder was found\n");
				return;
			}

			_configurationPath = (_saveFolder + "\\") + launcherConfigFile;
		}
	}

	HMODULE Installation::LoadLib(const char* name, LoadableDlls dll) {
		/* Ideally, we should load using LOAD_LIBRARY_AS_IMAGE_RESOURCE, however
		 * this causes GetProcAddress to not work. DONT_RESOLVE_DLL_REFERENCED, 
		 * though deprecated, it the safest I can come up with, barring a full
		 * load of the DLL (that will fail because Lua5.4.dll is not in the
		 * PATH), or scanning the DLL myself (come on GetProcAddress, just work).
		 */
		HMODULE lib = LoadLibraryExA(name, NULL, DONT_RESOLVE_DLL_REFERENCES);
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

	std::string const& Installation::GetZHLLoaderVersion() const {
		return _zhlLoaderVersion;
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
		std::string path;
		Filesystem::SaveFolderResult result = Filesystem::GetIsaacSaveFolder(&path);
		switch (result) {
		case Filesystem::SAVE_FOLDER_ERR_USERPROFILE:
		case Filesystem::SAVE_FOLDER_ERR_GET_USER_PROFILE_DIR:
			return INSTALLATION_PATH_INIT_ERR_PROFILE_DIR;

		case Filesystem::SAVE_FOLDER_DOES_NOT_EXIST:
			return INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR;

		default:
			break;
		}

		_saveFolder = std::move(path);
		return INSTALLATION_PATH_INIT_OK;
	}

	IsaacInstallationPathInitResult Installation::SearchConfigurationFile() {
		std::string path = _saveFolder;
		if (path.empty()) {
			return INSTALLATION_PATH_INIT_ERR_NO_SAVE_DIR;
		}

		path += "\\";
		path += launcherConfigFile;
		_configurationPath = std::move(path);

		if (!Filesystem::FileExists(_configurationPath.c_str())) {
			Logger::Error("No launcher configuration file found in Repentance save folder\n");
			return INSTALLATION_PATH_INIT_ERR_NO_CONFIG;
		}

		IsaacInstallationPathInitResult parseRes = ProcessConfigurationFile(_configurationPath);
		if (parseRes != INSTALLATION_PATH_INIT_OK) {
			Logger::Error("Error while opening/parsing the launcher configuration file\n");
			return parseRes;
		}

		
		Logger::Info("Found configuration file %s\n", _configurationPath.c_str());
		return parseRes;
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
			} else {
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

	void Installation::WriteLauncherConfigurationFile() {
		if (_configurationPath.empty()) {
			Logger::Fatal("Installation::WriteLauncherConfigurationFile called, but configuration path was not properly initialized before\n");
			throw std::runtime_error("Inconsistent runtime state: attempted to write launcher configuration without a valid path");
		}

		if (_isaacFolder.empty()) {
			Logger::Fatal("Installation::WriteLauncherConfigurationFile: _isaacFolder is empty\n");
			throw std::runtime_error("Inconsistent runtime state: attempted to write launcher configuration without a valid Isaac folder");
		}

		std::ofstream configuration(_configurationPath, std::ios::out);
		configuration << "[" << fs::Configuration::GeneralSection << "]" << std::endl;
		configuration << fs::Configuration::IsaacFolderKey << " = " << _isaacFolder << std::endl;
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
	}
}