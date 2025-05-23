#include <WinSock2.h>
#include <Windows.h>
#include <io.h>

#include <fstream>
#include <filesystem>
#include <stdexcept>

#include "launcher/isaac_installation.h"
#include "launcher/repentogon_installation.h"
#include "shared/filesystem.h"
#include "shared/logger.h"
#include "shared/scoped_file.h"
#include "shared/sha256.h"
#include "shared/steam.h"
#include "shared/unique_free_ptr.h"
#include "launcher/standalone_rgon_folder.h"

static constexpr const char* __versionNeedle = "Binding of Isaac: Repentance+ v";

namespace srgon = standalone_rgon;

Version const knownVersions[] = {
		{ "fd5b4a2ea3b397aec9f2b311ecc3be2e7e66bcd8723989096008d4d8258d92ea", "v1.9.7.10.J212 (Steam)", true },
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

bool IsaacInstallation::ValidateExecutable(std::string const& path) {
	_isValid = false;

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

	_isValid = true;
	return true;
}

std::optional<std::string> IsaacInstallation::AutoDetect() {
	std::string path;
	if (Filesystem::FileExists("isaac-ng.exe")) {
		path = Filesystem::GetCurrentDirectory_() + "\\isaac-ng.exe";
	} else {
		std::optional<std::string> steamPath = Steam::GetSteamInstallationPath();
		if (!steamPath) {
			return std::nullopt;
		}

		path = *steamPath + "\\steamapps\\common\\The Binding of Isaac Rebirth\\isaac-ng.exe";
	}

	if (Validate(path)) {
		return path;
	}

	return std::nullopt;
}

bool IsaacInstallation::Validate(std::string const& sourcePath) {
	std::string path = sourcePath;
	if (path.empty()) {
		Logger::Error("IsaacInstallation::Validate: received empty path\n");
		return false;
	} else {
		Logger::Info("Installation::Validate: validating %s\n", path.c_str());
	}

	if (!ValidateExecutable(path)) {
		_gui->LogError("%s is not a valid Isaac executable\n", path.c_str());
		Logger::Error("Installation::Validate: invalid file given\n");
		return false;
	}

	std::optional<std::variant<const Version*, std::string>> version = ComputeVersion(path);
	if (!version) {
		_gui->LogError("Unable to compute version of executable %s, rejecting it as invalid\n", path.c_str());
		Logger::Error("Installation::Validate: unable to get version of executable\n");
		return false;
	}

	_version = std::move(*version);
	_exePath = path;
	std::filesystem::path fsPath(path);
	fsPath.remove_filename();
	_folderPath = fsPath.string();
	_isCompatibleWithRepentogon = CheckCompatibilityWithRepentogon();

	if (_isCompatibleWithRepentogon && !srgon::exeIsCopy(path)) {
		std::filesystem::path fsTempPath(path);
		std::filesystem::path copyexepath = srgon::getCopyExePath(fsTempPath);
		if (!srgon::exeCopyExists(path) || !Validate(copyexepath.string())) {
			srgon::copyFiles(path, _needspatch);
			_isCompatibleWithRepentogon = true;
			_version = std::move(*version);
		}
		_exePath = copyexepath.string();
		copyexepath.remove_filename();
		_folderPath = copyexepath.string();
	}
	else if (!_isCompatibleWithRepentogon && srgon::exeIsCopy(path)) {
		Validate(fsPath.string());
	}

	return true;
}

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

IsaacInstallation::IsaacInstallation(ILoggableGUI* gui) : _gui(gui) { }

std::optional<std::string> IsaacInstallation::GetVersionStringFromMemory(std::string const& path) {
	if (path.empty()) {
		Logger::Error("Installation::GetVersionString: no executable\n");
		return std::nullopt;
	}

	FILE* exe = fopen(path.c_str(), "rb");
	if (!exe) {
		Logger::Error("Installation::GetVersionString: could not open %s\n", path.c_str());
		return std::nullopt;
	}

	ScopedFile scopedFile(exe);

	DWORD highOrder = 0;
	HANDLE exeHandle = (HANDLE)_get_osfhandle(_fileno(exe));
	DWORD size = GetFileSize(exeHandle, &highOrder);
	DWORD minSize = 0x40; // DOS header

	if (highOrder != 0) {
		Logger::Error("Installation::GetVersionString: isaac-ng.exe size > 4GB\n");
		return std::nullopt;
	}

	if (size < minSize) {
		Logger::Error("Installation::GetVersionString: executable too short (< 0x40 bytes)\n");
		return std::nullopt;
	}

	char* content = (char*)malloc(size);
	if (!content) {
		Logger::Error("Installation::GetVersionString: unable to allocate memory to store isaac-ng.exe\n");
		return std::nullopt;
	}

	unique_free_ptr<char> scopedPtr(content);

	if (fread(content, size, 1, exe) != 1) {
		Logger::Error("Installation::GetVersionString: error while reading file content into memory\n");
		return std::nullopt;
	}

	uint32_t* peOffset = (uint32_t*)(content + 0x3c);
	minSize = *peOffset + 0x4; // DOS header + PE signature
	if (size <= minSize) {
		Logger::Error("Installation::GetVersionString: executable too short (no PE header)\n");
		return std::nullopt;
	}

	uint32_t* peSignaturePtr = (uint32_t*)(content + *peOffset);
	if (*peSignaturePtr != 0x00004550) {
		Logger::Error("Installation::GetVersionString: executable is not a PE executable\n");
		return std::nullopt;
	}

	minSize += 20; // DOS header + PE signature + COFF Header
	if (size < minSize) {
		Logger::Error("Installation::GetVersionString: executable too short (no COFF header)\n");
		return std::nullopt;
	}

	COFFHeader* coffHeader = (COFFHeader*)(content + *peOffset + 0x4);

	minSize += coffHeader->SizeOfOptionalHeader;
	if (size < minSize) {
		Logger::Error("Installation::GetVersionString: executable too short (malformed optional header)\n");
		return std::nullopt;
	}

	SectionHeader* sections = (SectionHeader*)(content + minSize);
	minSize += coffHeader->NumberOfSections * sizeof(*coffHeader);

	if (size < minSize) {
		Logger::Error("Installation::GetVersionString: executable too short (malformed section headers)\n");
		return std::nullopt;
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
		return std::nullopt;
	}

	char* sectionStart = content + rdataSectionHeader->PointerToRawData;
	if (size < rdataSectionHeader->PointerToRawData + rdataSectionHeader->SizeOfRawData) {
		Logger::Error("Installation::GetVersionString: executable too short (malformed rdata section header)\n");
		return std::nullopt;
	}

	size_t needleLen = strlen(__versionNeedle);
	if (rdataSectionHeader->SizeOfRawData < needleLen) {
		Logger::Error("Installation::GetVersionString: executable too short (version string does not fit)\n");
	}

	size_t limit = rdataSectionHeader->SizeOfRawData - needleLen;
	uint32_t offset = -1;
	for (uint32_t i = 0; i < limit; ++i) {
		if (memcmp(sectionStart + i, __versionNeedle, needleLen))
			continue;
		offset = i;
		break;
	}

	if (offset == -1) {
		Logger::Error("Installation::GetVersionString: invalid executable (no version string found)\n");
		return std::nullopt;
	}

	char* startVersion = sectionStart + offset;
	char* endVersion = startVersion;

	while (*endVersion && endVersion < sectionStart + limit)
		++endVersion;

	if (*endVersion) {
		Logger::Error("Installation::GetVersionString: invalid executable (version string goes out of rdata section)\n");
		return std::nullopt;
	}

	std::string result(startVersion, endVersion);
	return result;
}

std::optional<std::string> IsaacInstallation::ComputeVersion(std::string const& path) {
	std::optional<std::string> versionString = GetVersionStringFromMemory(path);
	// Can't use a ternary because type unification is tricky here
	if (versionString) {
		return StripVersion(*versionString);
	} else {
		return std::nullopt;
	}
}

bool IsaacInstallation::CheckCompatibilityWithRepentogon() {
	namespace fs = std::filesystem;

	fs::path fullPath = fs::current_path() / "patch/version.txt";
	if (fs::exists(fullPath)) {
		std::ifstream file(fullPath);
		if (!file) {
			std::cerr << "Failed to open file: " << fullPath << std::endl;
			return "";
		}

		std::ostringstream ss;
		ss << file.rdbuf();  // Read the whole file into the stringstream
		if (ss.str() == GetVersion()) {
			_needspatch = true;
			return true;
		}
	}
	return RepentogonInstallation::IsIsaacVersionCompatible(GetVersion());
}

std::string IsaacInstallation::StripVersion(std::string const& version) {
	size_t len = strlen(__versionNeedle);
	return version.substr(len - 1, version.size() - len);
}