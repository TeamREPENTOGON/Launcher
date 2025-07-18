#include <WinSock2.h>
#include <Windows.h>
#include <io.h>

#include <fstream>
#include <filesystem>
#include <regex>
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
namespace fs = std::filesystem;

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

bool InstallationData::ValidateExecutable(std::string const& path) {
	_isValid = DoValidateExecutable(path);
	return _isValid;
}

bool InstallationData::DoValidateExecutable(std::string const& path) const {
	if (!Filesystem::Exists(path.c_str())) {
		_gui->LogError("BoI executable %s does not exist\n", path.c_str());
		Logger::Error("Installation::CheckIsaacExecutable: executable %s does not exist\n", path.c_str());
		return false;
	}

	DWORD subsystem = 0;
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

// Parses alternate Steam library paths from a config.vdf file (IE, libraries on different drives than the Steam installation itself).
std::vector<std::string> ParseLibraryPathsFromSteamConfigFile(const std::string& steamConfigPath) {
	std::vector<std::string> out;

	std::ifstream file(steamConfigPath);
	if (file.is_open()) {
		std::string line;
		while (std::getline(file, line)) {
			const std::regex rgx(R"(\s*\"BaseInstallFolder_\d+\"\s+\"([^\s"]+)\"*)");
			std::smatch match;
			if (std::regex_search(line, match, rgx) && match.size() > 1) {
				const std::string path = match[1];
				// Replace `\\` in the path with `\`
				const std::string normalizedPath = std::regex_replace(path, std::regex(R"(\\\\)"), "\\");
				out.push_back(normalizedPath);
			}
		}
		file.close();
	} else {
		Logger::Error("ParseLibraryPathsFromSteamConfigFile: Failed to open %s\n", steamConfigPath.c_str());
	}

	return out;
}

// Attempts to locate an isaac-ng.exe file from Steam. First checks Steam's install directory,
// then checks for alternate Steam Libraries on other drives.
std::optional<std::string> LocateSteamIsaacExecutable() {
	// Get the installation path of Steam itself.
	std::optional<std::string> steamInstallPath = Steam::GetSteamInstallationPath();
	if (!steamInstallPath) {
		Logger::Info("LocateSteamIsaacExecutable: No Steam installation path found.\n");
		return std::nullopt;
	}
	Logger::Info("LocateSteamIsaacExecutable: Found Steam installation @ %s\n", steamInstallPath->c_str());

	constexpr char isaacRelativePath[] = "\\steamapps\\common\\The Binding of Isaac Rebirth\\isaac-ng.exe";

	// Check for an isaac-ng.exe under the Steam installation directory.
	const std::string steamInstallIsaacPath = *steamInstallPath + isaacRelativePath;

	if (Filesystem::Exists(steamInstallIsaacPath.c_str())) {
		Logger::Info("LocateSteamIsaacExecutable: isaac-ng.exe found @ %s\n", steamInstallIsaacPath.c_str());
		return steamInstallIsaacPath;
	}
	Logger::Info("LocateSteamIsaacExecutable: isaac-ng.exe not found under Steam installation directory.\n");

	// Parse Steam's config.vdf file to locate Steam Libraries located on other drives, and look for isaac-ng.exe in those.
	const std::string steamConfigPath = *steamInstallPath + "\\config\\config.vdf";

	if (!Filesystem::Exists(steamConfigPath.c_str())) {
		Logger::Info("LocateSteamIsaacExecutable: config.vdf not found @ %s\n", steamConfigPath.c_str());
		return std::nullopt;
	}

	Logger::Info("LocateSteamIsaacExecutable: Checking config.vdf for alternate Steam libraries...\n");

	for (const std::string steamLibraryPath : ParseLibraryPathsFromSteamConfigFile(steamConfigPath)) {
		Logger::Info("LocateSteamIsaacExecutable: Checking Steam library @ %s\n", steamLibraryPath.c_str());

		const std::string steamLibraryIsaacPath = steamLibraryPath + isaacRelativePath;

		if (Filesystem::Exists(steamLibraryIsaacPath.c_str())) {
			Logger::Info("LocateSteamIsaacExecutable: isaac-ng.exe found @ %s\n", steamLibraryIsaacPath.c_str());
			return steamLibraryIsaacPath;
		}
	}

	Logger::Info("LocateSteamIsaacExecutable: Could not locate isaac-ng.exe\n");
	return std::nullopt;
}

std::optional<std::string> IsaacInstallation::AutoDetect(bool* standalone) {
	std::string path;
	if (Filesystem::Exists("isaac-ng.exe")) {
		path = Filesystem::GetCurrentDirectory_() + "\\isaac-ng.exe";
	} else {
		std::optional<std::string> steamIsaacPath = LocateSteamIsaacExecutable();
		if (!steamIsaacPath) {
			return std::nullopt;
		}

		path = *steamIsaacPath;
	}

	if (Validate(path, standalone)) {
		return path;
	}

	return std::nullopt;
}

bool InstallationData::Validate(std::string const& sourcePath, bool repentogon,
	bool* standalone) {
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
	Logger::Info("InstallationData::Validate: validated executable %s (version '%s') (repentogon = %d)\n",
		path.c_str(), GetVersion(), repentogon);
	_exePath = path;
	std::filesystem::path fsPath(path);
	fsPath.remove_filename();
	_folderPath = fsPath.string();

	/* Consider the installation compatible if it is the supported version,
	 * OR if a patch exists to convert it to the supported version.
	 *
	 * Existing Repentogon installations should not need patching.
	 */
	_needsPatch = !repentogon && PatchIsAvailable();
	_isCompatibleWithRepentogon = _needsPatch || RepentogonInstallation::IsIsaacVersionCompatible(GetVersion());

	if (srgon::IsStandaloneFolder(sourcePath) && !repentogon) {
		_gui->LogWarn("The main Isaac executable is located in a Repentogon installation, this may indicate a broken configuration\n");
		Logger::Warn("Main Isaac executable %s is located inside a Repentogon installation", path.c_str());

		if (standalone) {
			*standalone = true;
		}
	} else if (standalone) {
		*standalone = false;
	}

	return true;
}

bool IsaacInstallation::Validate(std::string const& sourcePath, bool* standalone) {
	InstallationData data;
	data.SetGUI(_gui);

	if (!data.Validate(sourcePath, false, standalone)) {
		return false;
	}

	_mainInstallation = data;

	return true;
}

bool IsaacInstallation::ValidateRepentogon(std::string const& folder) {
	InstallationData data;
	data.SetGUI(_gui);

	fs::path exePath(_mainInstallation.GetExePath());

	if (!data.Validate((folder + "\\") + exePath.filename().string(), true, nullptr)) {
		return false;
	}

	_repentogonInstallation = data;
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

std::optional<std::string> InstallationData::GetVersionStringFromMemory(std::string const& path) {
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
	int32_t offset = -1;
	for (int32_t i = 0; i < limit; ++i) {
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

std::optional<std::string> InstallationData::ComputeVersion(std::string const& path) {
	std::optional<std::string> versionString = GetVersionStringFromMemory(path);
	// Can't use a ternary because type unification is tricky here
	if (versionString) {
		return StripVersion(*versionString);
	} else {
		return std::nullopt;
	}
}

bool InstallationData::PatchIsAvailable() const {
	namespace fs = std::filesystem;

	fs::path fullPath = fs::current_path() / "patch/version.txt";
	std::string s = fullPath.string();
	if (fs::exists(fullPath)) {
		ScopedFile file(fopen(s.c_str(), "r"));
		if (!file) {
			Logger::Error("InstallationData::CheckCompatibilityWithRepentogon: failed to open patch file %s\n", s.c_str());
			return false;
		}

		long begin = ftell(file);
		fseek(file, 0, SEEK_END);

		if (ferror(file)) {
			Logger::Error("InstallationData::CheckCompatibilityWithRepentogon: failed to fseek\n");
			return false;
		}

		long end = ftell(file);
		rewind(file);

		std::unique_ptr<char[]> content = std::make_unique<char[]>(end - begin + 1);
		if (!content) {
			Logger::Error("InstallationData::CheckCompatibilityWithRepentogon: unable to allocate memory\n");
			return false;
		}

		fread(content.get(), end - begin, 1, file);
		content.get()[end - begin] = '\0';

		if (!strcmp(GetVersion(), content.get())) {
			return true;
		}
	}

	return false;
}

std::string InstallationData::StripVersion(std::string const& version) {
	size_t len = strlen(__versionNeedle);
	return version.substr(len - 1, version.size() - len);
}