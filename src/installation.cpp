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
#include "shared/externals.h"
#include "shared/logger.h"
#include "shared/filesystem.h"
#include "shared/scoped_file.h"
#include "shared/sha256.h"

namespace Launcher {
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

	#pragma pack(1)
	struct COFFHeader {
		uint16_t Machine;
		uint16_t NumberOfSections;
		uint32_t TimeDateStamp;
		void* PointerToSymbolTable;
		uint32_t NumberOfSymbols;
		uint16_t SizeOfOptionalHeader;
		uint16_t Characteristics;
	};

	static_assert(sizeof(COFFHeader) == 20);

	#pragma pack(1)
	struct SectionHeader {
		char Name[8];
		uint32_t VirtualSize;
		uint32_t VirtualAddress;
		uint32_t SizeOfRawData;
		uint32_t PointerToRawData;
		uint32_t PointerToRelocations;
		uint32_t PointerToLineNumbers;
		uint16_t NumberOfRelocations;
		uint16_t NumberOfLineNumbers;
		uint32_t Characteristics;
	};

	static_assert(sizeof(SectionHeader) == 40);

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

	Installation::Installation(ILoggableGUI* gui) : _gui(gui) {

	}

	bool Installation::InitFolders() {
		if (!SearchIsaacSaveFolder()) {
			return false;
		}
			
		if (!SearchConfigurationFile()) {
			return false;
		}

		if (!ProcessConfigurationFile(_configurationPath)) {
			_gui->LogWarn("Launcher configuration file contains errors, default options will be used\n");
			Logger::Error("Error while opening/parsing the launcher configuration file\n");
			return false;
		}

		return SearchIsaacInstallationPath();
	}

	bool Installation::CheckIsaacExecutable(std::string const& path) {
		if (!Filesystem::FileExists(path.c_str())) {
			_gui->LogError("BoI executable %s does not exist\n", path.c_str());
			Logger::Error("Installation::CheckIsaacExecutable: executable %s does not exist\n", path.c_str());
			return false;
		}

		DWORD subsystem = -1;
		if (!GetBinaryTypeA(path.c_str(), &subsystem)) {
			_gui->LogError("BoI executable %s is not executable !\n", path.c_str());
			Logger::Error("Installation::CheckIsaacExecutable: file %s is not executable\n", path.c_str());
			return false;
		}

		if (subsystem != SCS_32BIT_BINARY) {
			_gui->LogError("BoI executable %s is not a 32-bit Windows application !\n", path.c_str());
			Logger::Error("Installation::CheckIsaacExecutable: file %s is not a Win32 application\n", path.c_str());
			return false;
		}

		return true;
	}

	bool Installation::ConfigureIsaacExecutableFile(std::string const& path) {
		if (!CheckIsaacExecutable(path)) {
			_gui->LogError("%s is not a valid Isaac executable\n", path.c_str());
			Logger::Error("Installation::ConfigurationIsaacInstallationFile: invalid file given\n");
			return false;
		}

		try {
			std::string hash = Sha256::Sha256F(path.c_str());
			_isaacVersion = GetIsaacVersionFromHash(hash.c_str());
			Logger::Info("Computed hash of isaac-ng.exe: %s\n", hash.c_str());
		} catch (std::runtime_error& e) {
			Logger::Error("Updater::CheckIsaacInstallation: exception: %s\n", e.what());
		}

		/* Not an error to not have _isaacVersion here : second attempt will be 
		 * performed later, state remains consistent.
		 */
		_isaacExecutable = path;
		std::filesystem::path fsPath(path);
		fsPath.remove_filename();
		_isaacFolder = fsPath.string();

		CheckRepentogonInstallation();

		return true;
	}

	ScopedModule Installation::LoadModule(const char* shortName, const char* path, LoadableDlls dll) {
		HMODULE lib = LoadLib(path, dll);
		ScopedModule module(lib);
		if (!module) {
			Logger::Error("Installation::LoadModule: Failed to load %s (%d)\n", shortName, GetLastError());
		} else {
			Logger::Info("Installation::LoadModule: Loaded %s\n", shortName);
		}

		return module;
	}

	FARPROC Installation::RetrieveSymbol(HMODULE module, const char* libname, const char* symbol) {
		FARPROC result = GetProcAddress(module, symbol);
		if (!result) {
			Logger::Warn("Installation::RetrieveSymbol: %s does not export %s\n", libname, symbol);
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
				return false;
			} else {
				Logger::Info("Identified %s version %s\n", libname, target.c_str());
			}
		}

		return true;
	}

	bool Installation::CheckRepentogonInstallation() {
		std::string repentogonFolder = _isaacFolder;
		_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
		memset(_dllStates, LOAD_DLL_STATE_NONE, sizeof(_dllStates));

		_gui->Log("Checking for validity of Repentogon installation...\n");

		if (repentogonFolder.empty()) {
			_gui->LogError("Cannot check for a Repentogon installation without being given an Isaac executable\n");
			Logger::Critical("Installation::CheckRepentogonInstallation: function called without an Isaac folder, abnormal internal state\n");
			return false;
		}

		_repentogonFiles.clear();

		const char** mandatoryFile = mandatoryFileNames;
		bool ok = true;
		bool loaderMissing = false;
		while (*mandatoryFile) {
			std::string fullName = repentogonFolder + *mandatoryFile;
			FoundFile& file = _repentogonFiles.emplace_back();
			file.filename = *mandatoryFile;
			file.found = Filesystem::FileExists(fullName.c_str());

			if (!file.found) {
				Logger::Error("Updater::CheckRepentogonInstallation: %s not found\n", *mandatoryFile);
				loaderMissing = (strcmp(*mandatoryFile, Libraries::loader) == 0);
			}

			ok = ok && file.found;
			++mandatoryFile;
		}

		bool dsoundFound = Filesystem::FileExists((_isaacFolder + "\\dsound.dll").c_str());

		if (!ok) {
			if (loaderMissing && dsoundFound) {
				_gui->LogWarn("Installation of Repentogon is missing the ZHL loader DLL, but dsound.dll is present: possible legacy installation\n");
			} else {
				_gui->LogError("No valid Repentogon installation found in %s: missing files detected\n", _isaacFolder.c_str());
				return false;
			}
		}

		if (!loaderMissing && dsoundFound) {
			_gui->LogWarn("Installation of Repentogon contains both the ZHL loader and dsound.dll. For safety, the launcher will ignore the ZHL loader.\n");
		}

		ScopedModule loader = LoadModule(Libraries::loader, (repentogonFolder + Libraries::loader).c_str(), LOADABLE_DLL_ZHL_LOADER);
		if (!loader && !loaderMissing) {
			_gui->LogError("No valid Repentogon installation found: ZHL loader missing\n");
			return false;
		}

		ScopedModule zhl = LoadModule(Libraries::zhl, (repentogonFolder + Libraries::zhl).c_str(), LOADABLE_DLL_LIBZHL);
		if (!zhl) {
			_gui->LogError("No valid Repentogon installation found: ZHL DLL missing\n");
			return false;
		}

		ScopedModule repentogon = LoadModule(Libraries::repentogon, (repentogonFolder + Libraries::repentogon).c_str(), LOADABLE_DLL_REPENTOGON);
		if (!repentogon) {
			_gui->LogError("No valid Repentogon installation found: Repentogon DLL missing\n");
			return false;
		}

		FARPROC zhlVersion			= RetrieveSymbol(zhl,			Libraries::zhl,			Symbols::zhlVersion);
		FARPROC repentogonVersion	= RetrieveSymbol(repentogon,	Libraries::repentogon,	Symbols::repentogonVersion);
		FARPROC loaderVersion		= NULL;
		if (!loaderMissing) {
			loaderVersion			= RetrieveSymbol(loader, Libraries::loader, Symbols::loaderVersion);
		}

		if (!zhlVersion || !repentogonVersion || (!dsoundFound && !loaderVersion && !loaderMissing)) {
			_gui->LogError("No valid Repentogon installation found: some DLLs do not indicate a version\n");
			return false;
		}

		if (!loaderMissing && !dsoundFound) {
			if (!ValidateVersionSymbol(loader, Libraries::loader, Symbols::loaderVersion, loaderVersion, _zhlLoaderVersion)) {
				_gui->LogError("[DANGER] No valid Repentogon installation found: the ZHL loader DLL is malformed\n");
				return false;
			}
		}

		if (!ValidateVersionSymbol(zhl, Libraries::zhl, Symbols::zhlVersion, zhlVersion, _zhlVersion)) {
			_gui->LogError("[DANGER] No valid Repentogon installation found: the ZHL DLL is malformed\n");
			return false;
		}

		if (!ValidateVersionSymbol(repentogon, Libraries::repentogon, Symbols::repentogonVersion, repentogonVersion, _repentogonVersion)) {
			_gui->LogError("[DANGER] No valid Repentogon installation found: the Repentogon DLL is malformed\n");
			return false;
		}

		if (_zhlVersion != _repentogonVersion || (!dsoundFound && !loaderMissing && _repentogonVersion != _zhlLoaderVersion)) {
			if (!loaderMissing) {
				_gui->LogError("No valid Repentogon installation found: the ZHL loader, the ZHL DLL and the Repentogon DLL are not aligned on the same version\n");
				Logger::Warn("Updater::CheckRepentogonInstallation: ZHL / Repentogon version mismatch (ZHL version %s, ZHL loader version %s, Repentogon version %s)\n",
					_zhlVersion.c_str(), _zhlLoaderVersion.c_str(), _repentogonVersion.c_str());
			} else {
				_gui->LogError("No valid Repentogon installation found: the ZHL DLL and the Repentogon DLL are not aligned on the same version\n");
				Logger::Warn("Updater::CheckRepentogonInstallation: ZHL / Repentogon version mismatch (ZHL version %s, Repentogon version %s)\n",
					_zhlVersion.c_str(), _repentogonVersion.c_str());
			}

			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			_repentogonZHLVersionMatch = false;
			return false;
		}

		_repentogonZHLVersionMatch = true;

		if (dsoundFound) {
			_gui->LogWarn("Found a valid legacy installation of Repentogon (dsound.dll found)\n");
			_installationState = REPENTOGON_INSTALLATION_STATE_LEGACY;
		} else {
			_gui->Log("Found a valid installation of Repentogon (version %s)\n", _zhlVersion.c_str());
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
			_dllStates[dll] = LOAD_DLL_STATE_FAIL;
			return NULL;
		}

		_dllStates[dll] = LOAD_DLL_STATE_OK;
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

	LoadDLLState Installation::GetDLLLoadState(LoadableDlls dll) const {
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

	std::string const& Installation::GetIsaacExecutablePath() const {
		return _isaacExecutable;
	}

	std::string const& Installation::GetIsaacInstallationFolder() const {
		return _isaacFolder;
	}

	int Installation::GetConfigurationFileSyntaxErrorLine() const {
		return _configFileParseErrorLine;
	}

	std::unique_ptr<INIReader> const& Installation::GetConfigurationFileParser() const {
		return _configurationFile;
	}

	bool Installation::IsValidRepentogonInstallation(bool includeLegacy) const {
		return _installationState == REPENTOGON_INSTALLATION_STATE_MODERN ||
			(includeLegacy && _installationState == REPENTOGON_INSTALLATION_STATE_LEGACY);
	}

	bool Installation::IsLegacyRepentogonInstallation() const {
		return _installationState == REPENTOGON_INSTALLATION_STATE_LEGACY;
	}

	bool Installation::ProcessConfigurationFile(std::string const& path) {
		std::unique_ptr<INIReader> reader(new INIReader(path));
		if (int error = reader->ParseError(); error != 0) {
			switch (error) {
			case -1:
				_gui->LogWarn("The configuration file %s cannot be opened, default options will be used\n", path.c_str());
				Logger::Error("Unable to open launcher configuration file %s\n", path.c_str());
				return false;

			default:
				_configFileParseErrorLine = error;
				_gui->LogWarn("The configuration %s contains syntax errors, default options will be used\n", path.c_str());
				Logger::Error("INI parse error on line %d of configuration file %s\n", error, path.c_str());
				return false;
			}
		}

		_gui->LogInfo("The configuration file %s has been successfully read !\n", path.c_str());
		Logger::Info("Successfully opened and parsed launcher configuration file %s\n", path.c_str());
		_configurationFile = std::move(reader);
		return true;
	}

	bool Installation::SearchIsaacSaveFolder() {
		char homeDirectory[4096];
		DWORD homeLen = 4096;
		HANDLE token = GetCurrentProcessToken();

		if (!Externals::pGetUserProfileDirectoryA) {
			DWORD result = GetEnvironmentVariableA("USERPROFILE", homeDirectory, 4096);
			if (result < 0) {
				_gui->LogError("Unable to retrieve Isaac save folder path due to internal error\n");
				Logger::Error("Installation::GetIsaacSaveFolder: no GetUserProfileDirectoryA and no %USERPROFILE\n");
				return false;
			}
		} else {
			BOOL result = Externals::pGetUserProfileDirectoryA(token, homeDirectory, &homeLen);

			if (!result) {
				_gui->LogError("Unable to retrive Isaac save folder path due to internal error\n");
				Logger::Error("Installation::GetIsaacSaveFolder: unable to find user profile directory: %d\n", GetLastError());
				return false;
			}
		}

		std::string path(homeDirectory);
		path += "\\Documents\\My Games\\Binding of Isaac Repentance";

		if (!Filesystem::FolderExists(path.c_str())) {
			_gui->LogError("No Repentance save folder found, launch the game once without the launcher please\n");
			Logger::Error("Repentance save folder %s does not exist\n", path.c_str());
			return false;
		}

		_saveFolder = std::move(path);
		return true;
	}

	bool Installation::SearchConfigurationFile() {
		std::string path = _saveFolder;
		if (path.empty()) {
			_gui->LogInfo("No launcher configuration file found, default options will be used\n");
			Logger::Critical("Installation::SearchConfigurationFile: function called without a save folder, abnormal internal state\n");
			return false;
		}

		path += "\\";
		path += launcherConfigFile;
		_configurationPath = std::move(path);

		if (!Filesystem::FileExists(_configurationPath.c_str())) {
			_gui->LogInfo("No launcher configuration file found in Repentance save folder, default options will be used\n");
			Logger::Info("No launcher configuration file found in Repentance save folder\n");
			return false;
		}

		_gui->LogInfo("Found launcher configuration file %s\n", _configurationPath.c_str());
		Logger::Info("Found and processed configuration file %s\n", _configurationPath.c_str());
		return true;
	}

	bool Installation::SearchIsaacInstallationPath() {
		if (!_configurationFile) {
			_gui->LogWarn("No Isaac executable automatically found, you'll need to specify one\n");
			Logger::Critical("Installation::SearchIsaacInstallationPath: function called without a configuration file, abnormal internal state\n");
			return false;
		}

		std::string isaacExecutable = _configurationFile->GetString(Launcher::Configuration::GeneralSection, 
			Launcher::Configuration::IsaacExecutableKey, Launcher::Configuration::EmptyPath);
		if (isaacExecutable == Launcher::Configuration::EmptyPath) {
			bool ok = ConfigureIsaacExecutableFile(Filesystem::GetCurrentDirectory_() + "isaac-ng.exe");
			if (!ok) {
				_gui->LogWarn("The launcher configuration file does not indicate a path to the Isaac executable, you'll need to specify one\n");
				Logger::Error("Configuration file has no Isaac executable and no isaac-ng.exe found in current folder\n");
			} else {
				_gui->LogInfo("Autodetected Isaac executable %s\n", _isaacExecutable.c_str());
				Logger::Info("Autodetected Isaac executable %s\n", _isaacExecutable.c_str());
			}

			return ok;
		}

		if (!Filesystem::FileExists(isaacExecutable.c_str())) {
			_gui->LogWarn("The launcher configuration file indicates a non existent Isaac executable (%s)," 
				"you'll need to specify a correct one\n", isaacExecutable.c_str());
			Logger::Error("Isaac executable %s in launcher configuration file does not exist\n", isaacExecutable.c_str());
			return false;
		}

		_gui->LogInfo("Launcher configuration file indicates %s as Isaac executable, checking it is valid\n", isaacExecutable.c_str());
		if (!ConfigureIsaacExecutableFile(isaacExecutable)) {
			_gui->LogWarn("Isaac executable %s is not valid, you'll be prompted for a valid one\n", isaacExecutable.c_str());
			return false;
		}

		_gui->Log("Isaac executable %s is valid !\n", _isaacExecutable.c_str());
		Logger::Info("Found Isaac installation %s\n", _isaacExecutable.c_str());
		return true;
	}

	void Installation::WriteLauncherConfigurationFile() {
		if (_configurationPath.empty()) {
			Logger::Fatal("Installation::WriteLauncherConfigurationFile called, but configuration path was not properly initialized before\n");
			throw std::runtime_error("Inconsistent runtime state: attempted to write launcher configuration without a valid path");
		}

		if (_isaacExecutable.empty()) {
			Logger::Fatal("Installation::WriteLauncherConfigurationFile: _isaacExecutable is empty\n");
			throw std::runtime_error("Inconsistent runtime state: attempted to write launcher configuration without a valid Isaac folder");
		}

		std::ofstream configuration(_configurationPath, std::ios::out);
		configuration << "[" << Configuration::GeneralSection << "]" << std::endl;
		configuration << Configuration::IsaacExecutableKey << " = " << _isaacExecutable << std::endl;
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

	ReadVersionStringResult Installation::GetVersionString(std::string& result) const {
		if (_isaacExecutable.empty()) {
			Logger::Error("Installation::GetVersionString: no executable\n");
			return READ_VERSION_STRING_ERR_NO_FILE;
		}

		FILE* exe = fopen(_isaacExecutable.c_str(), "rb");
		if (!exe) {
			Logger::Error("Installation::GetVersionString: could not open %s\n", _isaacExecutable.c_str());
			return READ_VERSION_STRING_ERR_OPEN;
		}

		ScopedFile scopedFile(exe);
		
		DWORD highOrder = 0;
		HANDLE exeHandle = (HANDLE)_get_osfhandle(_fileno(exe));
		DWORD size = GetFileSize(exeHandle, &highOrder);
		DWORD minSize = 0x40; // DOS header

		if (highOrder != 0) {
			Logger::Error("Installation::GetVersionString: isaac-ng.exe size > 4GB\n");
			return READ_VERSION_STRING_ERR_TOO_BIG;
		}

		if (size < minSize) {
			Logger::Error("Installation::GetVersionString: executable too short (< 0x40 bytes)\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		char* content = (char*)malloc(size);
		if (!content) {
			Logger::Error("Installation::GetVersionString: unable to allocate memory to store isaac-ng.exe\n");
			return READ_VERSION_STRING_ERR_NO_MEM;
		}

		uint32_t* peOffset = (uint32_t*)(content + 0x3c);
		minSize = *peOffset + 0x4; // DOS header + PE signature
		if (size <= minSize) {
			Logger::Error("Installation::GetVersionString: executable too short (no PE header)\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		uint32_t* peSignaturePtr = (uint32_t*)(content + *peOffset);
		if (*peSignaturePtr != 0x50450000) {
			Logger::Error("Installation::GetVersionString: executable is not a PE executable\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		minSize += 20; // DOS header + PE signature + COFF Header
		if (size < minSize) {
			Logger::Error("Installation::GetVersionString: executable too short (no COFF header)\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}
 
		COFFHeader* coffHeader = (COFFHeader*)(content + *peOffset + 0x4);
		
		minSize += coffHeader->SizeOfOptionalHeader;
		if (size < minSize) {
			Logger::Error("Installation::GetVersionString: executable too short (malformed optional header)\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		SectionHeader* sections = (SectionHeader*)(content + minSize);
		minSize += coffHeader->NumberOfSections * sizeof(*coffHeader);

		if (size < minSize) {
			Logger::Error("Installation::GetVersionString: executable too short (malformed section headers)\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		SectionHeader* rdataSectionHeader = NULL;
		for (uint16_t i = 0; i < coffHeader->NumberOfSections; ++i) {
			SectionHeader* currentSection = sections + i;
			if (!strncmp(".rdata", currentSection->Name, 8)) {
				rdataSectionHeader = currentSection;
				break;
			}
		}

		if (!rdataSectionHeader) {
			Logger::Error("Installation::GetVersionString: no .rdata section found\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		char* sectionStart = content + rdataSectionHeader->PointerToRawData;
		if (size < rdataSectionHeader->PointerToRawData + rdataSectionHeader->SizeOfRawData) {
			Logger::Error("Installation::GetVersionString: executable too short (malformed rdata section header)\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		const char* needle = "Binding of Isaac: Repentance v";
		size_t needleLen = strlen(needle);
		if (rdataSectionHeader->SizeOfRawData < needleLen) {
			Logger::Error("Installation::GetVersionString: executable too short (version string does not fit)\n");
		}

		size_t limit = rdataSectionHeader->SizeOfRawData - needleLen;
		uint32_t offset = -1;
		for (uint32_t i = 0; i < limit; ++i) {
			if (memcmp(sectionStart + i, needle, needleLen))
				continue;
			offset = i;
			break;
		}
		
		if (offset != -1) {
			Logger::Error("Installation::GetVersionString: invalid executable (no version string found)\n");
			return READ_VERSION_STRING_ERR_NO_VERSION;
		}

		char* startVersion = sectionStart + offset;
		char* endVersion = startVersion;

		while (*endVersion && endVersion < sectionStart + limit)
			++endVersion;

		if (*endVersion) {
			Logger::Error("Installation::GetVersionString: invalid executable (version string goes out of rdata section)\n");
			return READ_VERSION_STRING_ERR_INVALID_PE;
		}

		free(content);
		result = std::string(startVersion, endVersion);
		return READ_VERSION_STRING_OK;
	}

	bool Installation::IsCompatibleWithRepentogon() {
		Version const* version = GetIsaacVersion();
		bool compatible = false;
		if (!version) {
			_gui->LogWarn("This installation of Isaac is non-standard (could be the result of running a Steamless version)\n"
				"Attempting to determine its version to check compatibility with Repentogon\n");
			std::string versionStr;
			ReadVersionStringResult result = GetVersionString(versionStr);

			if (result != READ_VERSION_STRING_OK) {
				_gui->LogError("An error occured while attempting to determine the version of Isaac. Assuming it is not REPENTOGON compatible\n");
				return false;
			}

			_gui->LogInfo("Identified Isaac version %s\n", versionStr.c_str());
			compatible = IsCompatibleWithRepentogon(versionStr);
		} else {
			_gui->LogInfo("Identified Isaac version %s\n", version->version);
			compatible = version->valid;
		}

		if (compatible) {
			_gui->LogInfo("This version is compatible with Repentogon !\n");
		} else {
			_gui->LogWarn("This version is incompatible with Repentogon. Disabling Repentogon launch options.\n");
		}

		return compatible;
	}

	bool Installation::IsCompatibleWithRepentogon(std::string const& ver) {
		_gui->LogError("Called Installation::IsCompatibleWithRepentogon before it is finalized\n");
		return false;
	}
}