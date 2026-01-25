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
#include "shared/pe32.h"
#include "shared/scoped_file.h"
#include "shared/sha256.h"
#include "shared/steam.h"
#include "shared/unique_free_ptr.h"
#include "shared/zip.h"
#include "launcher/standalone_rgon_folder.h"
#include "steam_api.h"


#include <io.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <string>
#include <cstdio>
#include <vector>
#include <curl/curl.h>

static constexpr const char* __versionNeedle = "Binding of Isaac: Repentance+ v";
static constexpr const char* __launcherDataFolder = "launcher-data";
static constexpr const char* __patchFolder = "launcher-data/patch";
static constexpr const char* __patchZip = "launcher-data/patch.zip";

namespace srgon = standalone_rgon;
namespace fs = std::filesystem;

/*
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
*/

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
	if (SteamAPI_Init() && SteamAPI_IsSteamRunning()) { //SteamAPI way (more reliable but will only work if steam is running)
		char temppath[MAX_PATH];
		SteamApps()->GetAppInstallDir(250900, temppath, sizeof(temppath));
		std::string stringedpath = std::string(temppath);
		if (stringedpath.length() > 0) {
			return std::string(temppath) + "\\isaac-ng.exe";
		}
	}

	// Get the installation path of Steam itself.
	const char* steamInstallPath = Steam::GetSteamInstallationPath();
	if (!steamInstallPath) {
		Logger::Info("LocateSteamIsaacExecutable: No Steam installation path found.\n");
		return std::nullopt;
	}
	Logger::Info("LocateSteamIsaacExecutable: Found Steam installation @ %s\n", steamInstallPath);

	constexpr char isaacRelativePath[] = "\\steamapps\\common\\The Binding of Isaac Rebirth\\isaac-ng.exe";

	// Check for an isaac-ng.exe under the Steam installation directory.
	const std::string steamInstallIsaacPath = std::string(steamInstallPath) + isaacRelativePath;

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
	if (_isCompatibleWithRepentogon) {
		if (_needsPatch) {
			_CompatReason = "Will work, the launcher has a patch to adapt it to a compatible version.";
		}
		else {
			_CompatReason = "Should just work.";
		}
	}
	else {
		if (!strncmp(GetVersion(), "v1.7", strlen("v1.7"))) {
			_CompatReason = "You need the Repentance+ dlc, you currently only have Repentance installed.";
		}
		else {
			_CompatReason = "You dont have a proper version or your exe is corrupted/cracked (verify your files with steam or download the specific version rgon uses)";
		}
	}

	bool isStandalone = srgon::IsStandaloneFolder(sourcePath);
	if (isStandalone && !repentogon) {
		_gui->LogWarn("The main Isaac executable is located in a Repentogon installation, this may indicate a broken configuration\n");
		Logger::Warn("Main Isaac executable %s is located inside a Repentogon installation", path.c_str());

		if (standalone) {
			*standalone = true;
		}
	} else if (standalone) {
		*standalone = isStandalone;
	}

	std::string steamAppidPath = _folderPath + "\\steam_appid.txt";
	if (repentogon && !Filesystem::Exists(steamAppidPath.c_str())) {
		Logger::Warn("Repentogon installation folder %s has no steam_appid.txt file, "
			"creating one\n", _folderPath.c_str());
		if (!srgon::CreateSteamAppIDFile(_folderPath)) {
			Logger::Error("Unable to create steam_appid.txt in Repentogon folder\n");
			return false;
		}
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

IsaacInstallation::IsaacInstallation(ILoggableGUI* gui) : _gui(gui) { }

std::optional<std::string> InstallationData::GetVersionStringFromMemory(std::string const& path) {
	try {
		PE32 pe32(path.c_str());
		auto [rdataSectionHeader, sectionStart] = pe32.GetSection(".rdata");

		if (!pe32.IsSectionSizeValid(rdataSectionHeader)) {
			Logger::Error("InstallationData::GetVersionStringFromMemory: .rdata section extends past file's bounds\n");
			return std::nullopt;
		}

		const char* startVersion = pe32.Lookup(__versionNeedle, rdataSectionHeader);
		if (!startVersion) {
			Logger::Info("Installation::GetVersionString: Version needle not found. Trying regex...\n");
			std::string sectionData(sectionStart, sectionStart + rdataSectionHeader->SizeOfRawData);
			const std::regex rgx("Binding of Isaac: [a-zA-Z]+[+]{0,1} (v[0-9a-zA-Z.]+)");
			std::smatch match;
			if (std::regex_search(sectionData, match, rgx) && match.size() > 1) {
				return match[1];
			}
			Logger::Error("Installation::GetVersionString: invalid executable (no version string found)\n");
			return std::nullopt;
		}
		const char* endVersion = startVersion;

		size_t needleLen = strlen(__versionNeedle);
		size_t limit = rdataSectionHeader->SizeOfRawData - needleLen;

		while (*endVersion && endVersion < (const char*)sectionStart + limit)
			++endVersion;

		if (*endVersion) {
			Logger::Error("Installation::GetVersionString: invalid executable (version string goes out of rdata section)\n");
			return std::nullopt;
		}

		return StripVersion(std::string(startVersion, endVersion));
	} catch (std::runtime_error const& e) {
		Logger::Error("InstallationData::GetVersionStringFromMemory: %s\n", e.what());
		return std::nullopt;
	}
}

std::optional<std::string> InstallationData::ComputeVersion(std::string const& path) {
	return GetVersionStringFromMemory(path);
}

static size_t patchwriteresponse(void* contents, size_t size, size_t nmemb, void* userp)
{
	FILE* fp = static_cast<FILE*>(userp);
	return fwrite(contents, size, nmemb, fp);
}

static void RemoveOldPatchFolder()
{
	namespace fs = std::filesystem;
	if (fs::exists(__patchFolder))
		fs::remove_all(__patchFolder);
}

bool CheckIfAvailableOnlineNGet(const std::string& currexehash,
	const std::string& targetversion)
{
	const std::string url =
		"https://gitlab.com/repentogon/versiontracker/-/raw/main/Patches/_" +
		currexehash + "_-_" + targetversion + "_.zip?inline=false";

	CURL* curl = curl_easy_init();
	if (!curl)
		return false;

	FILE* fp = fopen(__patchZip, "wb");
	if (!fp)
	{
		curl_easy_cleanup(curl);
		return false;
	}

	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "User-Agent: rgon-patch-getter/6.9");

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, patchwriteresponse);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

	CURLcode res = curl_easy_perform(curl);

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	fclose(fp);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK || http_code < 200 || http_code >= 300)
	{
		return false;
	}
	RemoveOldPatchFolder();
	if (!Zip::ExtractAllToFolder(__patchZip, __launcherDataFolder))
	{
		std::filesystem::remove(__patchZip);
		return false;
	}
	std::filesystem::remove(__patchZip);
	return true;
}



bool InstallationData::PatchIsAvailable(const bool skipOnlineCheck) {
	namespace fs = std::filesystem;
	if (_vanillaexeispatchable > 0) {
		return _vanillaexeispatchable == 1; //so it doesnt do the whole check and we can call this a shitton of times without worrying, the vanilla exe shouldnt change while the launcher is open anyway, since the launcher doesnt update it and...if it does, just fucking restart the launcher, dude
	}
	std::string vanillaexehash;
	HashResult result = Sha256::Sha256F(this->GetExePath().c_str(), vanillaexehash);

	if (result == HASH_OK) {
		fs::path fullPath = fs::current_path() / __patchFolder / "exehash.txt";
		std::string s = fullPath.string();
		if (fs::exists(fullPath)) {
			ScopedFile file(fopen(s.c_str(), "r"));
			if (!file) {
				Logger::Error("InstallationData::PatchIsAvailable: failed to open patch file %s\n", s.c_str());
				return false;
			}

			long begin = ftell(file);
			fseek(file, 0, SEEK_END);

			if (ferror(file)) {
				Logger::Error("InstallationData::PatchIsAvailable: failed to fseek\n");
				return false;
			}

			long end = ftell(file);
			rewind(file);

			std::unique_ptr<char[]> content = std::make_unique<char[]>(end - begin + 1);
			if (!content) {
				Logger::Error("InstallationData::PatchIsAvailable: unable to allocate memory\n");
				return false;
			}

			fread(content.get(), end - begin, 1, file);
			content.get()[end - begin] = '\0';

			std::string currexehash = content.get();
			std::transform(currexehash.begin(), currexehash.end(), currexehash.begin(),
				[](unsigned char c) { return std::tolower(c); });

			if (vanillaexehash == currexehash) {
				_vanillaexeispatchable = 1;
				return true;
			} else {
				Logger::Warn("InstallationData::PatchIsAvailable: Available patch's exe hash does not match the vanilla exe!\n");
			}
		} else {
			Logger::Warn("InstallationData::PatchIsAvailable: Patch files missing!\n");
		}
		if (skipOnlineCheck) {
			Logger::Warn("InstallationData::PatchIsAvailable: Skipping online check for available patches (possible failure/recursion).\n");
		} else {
			std::transform(vanillaexehash.begin(), vanillaexehash.end(), vanillaexehash.begin(),
				[](unsigned char c) { return std::toupper(c); });
			Logger::Info("InstallationData::PatchIsAvailable: Checking for patches online...\n");
			if (CheckIfAvailableOnlineNGet(vanillaexehash, patchtargetversion)) {
				Logger::Info("InstallationData::PatchIsAvailable: Potentially found usable patch online. Retrying...\n");
				return PatchIsAvailable(/*skipOnlineCheck=*/true);
			} else {
				Logger::Info("InstallationData::PatchIsAvailable: No online patch found.\n");
			}
		}
	}
	_vanillaexeispatchable = 0;
	return false;
}

std::string InstallationData::StripVersion(std::string const& version) {
	size_t len = strlen(__versionNeedle);
	return version.substr(len - 1, version.size() - len);
}