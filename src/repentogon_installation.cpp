#include <WinSock2.h>
#include <Windows.h>
#include <io.h>

#include "launcher/repentogon_installation.h"
#include "launcher/standalone_rgon_folder.h"
#include "shared/pe32.h"
#include "shared/scoped_module.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/module.h"

#include <iostream>
#include <fstream>

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

RepentogonInstallation::RepentogonInstallation(ILoggableGUI* gui) : _gui(gui) {
	ClearInstallation();
}

void RepentogonInstallation::ClearInstallation() {
	_installationState = REPENTOGON_INSTALLATION_STATUS_NONE;
	_repentogonVersion.clear();
	_zhlVersion.clear();
	_repentogonZHLVersionMatch = false;
	_dllHash.reset();
	_repentogonFiles.clear();
	memset(_dllStates, LOAD_DLL_STATE_NONE, sizeof(_dllStates));
}

bool RepentogonInstallation::CheckHalfAssedPatch(std::string const& installationPath) {
	std::string basepath = installationPath;
	std::string patchme = basepath + "patchme.daddy";

	//legacy files that may be fucked up
	std::string afterbirthpa = basepath + "resources\\packed\\afterbirthp.a.patched";
	std::string enumslua = basepath + "resources\\scripts\\enums.lua.patched";
	std::string jsonlua = basepath + "resources\\scripts\\json.lua.patched";
	std::string mainlua = basepath + "resources\\scripts\\main.lua.patched";
	//end of legacy files that may be fucked up

	return Filesystem::Exists(patchme.c_str()) || Filesystem::Exists(afterbirthpa.c_str()) || Filesystem::Exists(enumslua.c_str()) || Filesystem::Exists(jsonlua.c_str()) || Filesystem::Exists(mainlua.c_str());
}

bool RepentogonInstallation::CheckExeFuckMethod(std::string const& installationPath) {
	std::ostringstream stream;
	stream << installationPath << "/" << standalone_rgon::REPENTOGON_FUCK_METHOD_FILE;
	std::ifstream f(stream.str());

	if (!f) {
		return false;
	}

	uint32_t method;
	f >> method;

	if (method != standalone_rgon::REPENTOGON_FUCK_METHOD) {
		return false;
	}

	return ValidateFuckMethod(installationPath);
}

bool RepentogonInstallation::ValidateFuckMethod(std::string const& installationFolder) {
	std::filesystem::path p(installationFolder);
	p /= "Lua5.3.3r.dll";

	std::string asString = p.string();
	const char* path = asString.c_str();
	if (!std::filesystem::exists(p)) {
		Logger::Error("RepentogonInstallation::ValidateFuckMethod: %s does not exist\n", path);
		return false;
	}

	PE32 pe(path);
	ExportDirectoryTable* edt = (ExportDirectoryTable*)pe.RVA(pe.GetOptionalHeader()->ExportTable.VirtualAddress);
	uint32_t* namesPtr = (uint32_t*)pe.RVA(edt->NamePointerRVA);
	for (uint32_t i = 0; i < edt->NumberOfNamePointers; ++i) {
		const char* name = pe.RVA(namesPtr[i]);
		if (!strcmp(name, "LUA_REPENTOGON")) {
			return true;
		}
	}

	return false;
}

void RepentogonInstallation::InvalidateRepentogonExecutable(std::string const& executable) {
	Logger::Info("Invalidating Repentogon executable %s\n", executable.c_str());
	std::filesystem::path p(executable);
	if (!std::filesystem::exists(p)) {
		Logger::Error("Unable to invalidate executable: does not exist\n");
		return;
	}

	std::string asString = p.string();
	const char* path = asString.c_str();
	FILE* file = fopen(path, "rb");
	if (!file) {
		Logger::Error("Unable to invalidate executable: unable to open\n");
		return;
	}

	DWORD highOrder = 0;
	HANDLE exeHandle = (HANDLE)_get_osfhandle(_fileno(file));
	DWORD size = GetFileSize(exeHandle, &highOrder);

	if (highOrder != 0 || size < PE32::PE_SIGNATURE_OFFSET + 4) {
		Logger::Error("Unable to invalidate executable: invalid size\n");
		fclose(file);
		return;
	}

	char* content = (char*)calloc(1, size);
	if (!content) {
		Logger::Error("Unable to invalidate executable: no memory\n");
		fclose(file);
		return;
	}

	fread(content, size, 1, file);
	fclose(file);

	uint32_t offset = *(uint32_t*)(content + PE32::PE_SIGNATURE_OFFSET);
	if ((offset + 2) > size) {
		Logger::Error("Unable to invalidate executable: malformed headers\n");
		free(content);
		return;
	}

	char* pe = content + offset;
	if (pe[0] != 'P' || pe[1] != 'E') {
		Logger::Error("Unable to invalidate executable: invalid PE signature\n");
		free(content);
		return;
	}

	pe[0] = pe[1] = 0;
	file = fopen(path, "wb");
	if (!file) {
		Logger::Warn("Unable to invalidate executable: cannot open to write, removing instead\n");
		std::filesystem::remove(p);
		free(content);
		return;
	}

	fwrite(content, size, 1, file);
	fclose(file);
	free(content);
}

bool RepentogonInstallation::Validate(std::string const& installationFolder) {
	std::string repentogonFolder = installationFolder;
	repentogonFolder += "/";
	ClearInstallation();
	_installationState = REPENTOGON_INSTALLATION_STATUS_NONE;

	_gui->Log("Checking for validity of Repentogon installation...\n");

	if (repentogonFolder.empty()) {
		_gui->LogError("Cannot check for a Repentogon installation without being "
			"given an Isaac executable\n");
		Logger::Critical("RepentogonInstallation::Validate: function called without "
			"an Isaac folder, abnormal internal state\n");
		return false;
	}

	if (!Filesystem::Exists(repentogonFolder.c_str())) {
		_gui->Log("No Repentogon installation found in %s\n", repentogonFolder.c_str());
		Logger::Info("RepentogonInstallation::Validate: %s does not exist\n",
			repentogonFolder.c_str());
		return false;
	}

	_installationState = REPENTOGON_INSTALLATION_STATUS_BROKEN;
	if (CheckHalfAssedPatch(repentogonFolder)) {
		return false;
	}

	if (!CheckExeFuckMethod(repentogonFolder)) {
		_gui->LogError("The Repentogon installation is not protected against non launcher runs\n");
		InvalidateRepentogonExecutable(repentogonFolder + "isaac-ng.exe");
		return false;
	}

	const char** mandatoryFile = mandatoryFileNames;
	bool ok = true;
	bool loaderMissing = false;
	while (*mandatoryFile) {
		std::string fullName = repentogonFolder + *mandatoryFile;
		FoundFile& file = _repentogonFiles.emplace_back();
		file.filename = *mandatoryFile;
		file.found = Filesystem::Exists(fullName.c_str());

		if (!file.found) {
			Logger::Error("RepentogonInstallation::Validate: %s not found (%s)\n",
				*mandatoryFile, fullName.c_str());
			loaderMissing = (strcmp(*mandatoryFile, Libraries::loader) == 0);
		}

		ok = ok && file.found;
		++mandatoryFile;
	}

	bool dsoundFound = Filesystem::Exists((installationFolder + "\\dsound.dll").c_str());

	if (!ok) {
		_gui->LogError("No valid Repentogon installation found in %s: missing files detected\n", installationFolder.c_str());
		return false;
	}

	if (dsoundFound) {
		Logger::Error("RepentogonInstallation::Validate: found dsound.dll, malformed Repentogon installation\n");
		_gui->LogError("Found dsound.dll in %s, Repentogon installation is malformed\n",
			installationFolder.c_str());
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

	FARPROC zhlVersion = RetrieveSymbol(zhl, Libraries::zhl, Symbols::zhlVersion);
	FARPROC repentogonVersion = RetrieveSymbol(repentogon, Libraries::repentogon, Symbols::repentogonVersion);
	FARPROC loaderVersion = NULL;
	if (!loaderMissing) {
		loaderVersion = RetrieveSymbol(loader, Libraries::loader, Symbols::loaderVersion);
	}

	if (!zhlVersion || !repentogonVersion || (!dsoundFound && !loaderVersion && !loaderMissing)) {
		_gui->LogError("No valid Repentogon installation found: some DLLs do not indicate a version\n");
		return false;
	}

	if (!loaderMissing && !dsoundFound) {
		if (!ValidateVersionSymbol(loader, Libraries::loader, Symbols::loaderVersion,
			loaderVersion, _zhlLoaderVersion, _sZhlLoaderVersion)) {
			_gui->LogError("[DANGER] No valid Repentogon installation found: the ZHL loader DLL is malformed\n");
			return false;
		}
	}

	if (!ValidateVersionSymbol(zhl, Libraries::zhl, Symbols::zhlVersion,
		zhlVersion, _zhlVersion, _sZhlVersion)) {
		_gui->LogError("[DANGER] No valid Repentogon installation found: the ZHL DLL is malformed\n");
		return false;
	}

	if (!ValidateVersionSymbol(repentogon, Libraries::repentogon, Symbols::repentogonVersion,
		repentogonVersion, _repentogonVersion, _sRepentogonVersion)) {
		_gui->LogError("[DANGER] No valid Repentogon installation found: the Repentogon DLL is malformed\n");
		return false;
	}

	if (_zhlVersion != _repentogonVersion || (!dsoundFound && !loaderMissing && _repentogonVersion != _zhlLoaderVersion)) {
		if (!loaderMissing) {
			_gui->LogError("No valid Repentogon installation found: the ZHL loader, the ZHL DLL and the Repentogon DLL are not aligned on the same version\n");
			Logger::Error("RepentogonInstallation::Validate: ZHL / Repentogon version mismatch (ZHL version %s, ZHL loader version %s, Repentogon version %s)\n",
				_zhlVersion.c_str(), _zhlLoaderVersion.c_str(), _repentogonVersion.c_str());
		} else {
			_gui->LogError("No valid Repentogon installation found: the ZHL DLL and the Repentogon DLL are not aligned on the same version\n");
			Logger::Error("RepentogonInstallation::Validate: ZHL / Repentogon version mismatch (ZHL version %s, Repentogon version %s)\n",
				_zhlVersion.c_str(), _repentogonVersion.c_str());
		}

		_installationState = REPENTOGON_INSTALLATION_STATUS_NONE;
		_repentogonZHLVersionMatch = false;
		return false;
	}

	_repentogonZHLVersionMatch = true;
	_gui->Log("Found a valid installation of Repentogon (version %s)\n", _zhlVersion.c_str());
	Logger::Info("RepentogonInstallation::Validate: Repentogon installation in %s is valid\n",
		installationFolder.c_str());
	_installationState = REPENTOGON_INSTALLATION_STATUS_MODERN;

	return true;
}

ScopedModule RepentogonInstallation::LoadModule(const char* shortName, const char* path, LoadableDlls dll) {
	HMODULE lib = LoadLib(path, dll);
	ScopedModule module(lib);
	if (!module) {
		Logger::Error("Installation::LoadModule: Failed to load %s (%d)\n", shortName, GetLastError());
	} else {
		Logger::Info("Installation::LoadModule: Loaded %s\n", shortName);
	}

	return module;
}

FARPROC RepentogonInstallation::RetrieveSymbol(HMODULE module, const char* libname, const char* symbol) {
	FARPROC result = GetProcAddress(module, symbol);
	if (!result) {
		Logger::Warn("Installation::RetrieveSymbol: %s does not export %s\n", libname, symbol);
	} else {
		Logger::Info("Installation::RetrieveSymbol: found %s at %p\n", symbol, result);
	}

	return result;
}

bool RepentogonInstallation::ValidateVersionSymbol(HMODULE module, const char* libname, const char* symbolName,
	FARPROC symbol, std::string& target, std::string& shadowTarget) {
	shadowTarget = target;
	if (symbol) {
		Module::ValidateStringSymbolResult validateResult;
		std::string localTarget;
		if (!Module::ValidateStringSymbol(module, symbol, localTarget, &validateResult)) {
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
			shadowTarget = target;
			target = localTarget;
			Logger::Info("Identified %s version %s\n", libname, target.c_str());
		}
	}

	return true;
}

HMODULE RepentogonInstallation::LoadLib(const char* name, LoadableDlls dll) {
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

bool RepentogonInstallation::IsIsaacVersionCompatible(const char* version) {
	return !strcmp(version, "v1.9.7.12.J273");
}

void RepentogonInstallation::Invalidate() {
	_installationState = REPENTOGON_INSTALLATION_STATUS_NONE;
}