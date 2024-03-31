#include <Windows.h>
#include <bcrypt.h>
#include <Psapi.h>

#include <memory>
#include <sstream>
#include <stdexcept>

#include <curl/curl.h>

#include "launcher/curl_handler.h"
#include "launcher/logger.h"
#include "launcher/updater.h"

#include "zip.h"

namespace IsaacLauncher {
	/* Array of all the names of files that must be found for the installation 
	 * to be considered a valid Repentogon installation.
	 */
	static const char* mandatoryFileNames[] = {
		"libzhl.dll",
		"zhlRepentogon.dll",
		"freetype.dll",
		NULL
	};

	Version const knownVersions[] = {
		{ "04469d0c3d3581936fcf85bea5f9f4f3a65b2ccf96b36310456c9626bac36dc6", "v1.7.9b.J835 (Steam)", true },
		{ NULL, NULL, false }
	};

	/* GitHub URLs of the latest releases of Repentogon and the launcher. */
	static const char* RepentogonURL = "https://api.github.com/repos/TeamREPENTOGON/REPENTOGON/releases/latest";
	static const char* LauncherURL = "";

	static const char* RepentogonZipName = "REPENTOGON.zip";
	static const char* HashName = "hash.txt";

	/* Check if the latest release data is newer than the installed version.
	 * 
	 * tool is the name of the tool whose version is checked, installedVersion
	 * is the version of this tool, document is the JSON of the latest release
	 * of said tool.
	 */
	static VersionCheckResult CheckUpdates(const char* installedVersion, 
		const char* tool, rapidjson::Document& document);

	/* Fetch the content at url and store it in response.
	 * 
	 * The content is assumed to be the JSON of a GitHub release and as such 
	 * is expected to have a "name" field.
	 */
	static FetchUpdatesResult FetchUpdates(const char* url, 
		rapidjson::Document& response);

	/* Initialize a cURL session with sane parameters. 
	 * 
	 * url is the target address. handler is an instance of the generic class
	 * used to process the data received.
	 */
	static void InitCurlSession(CURL* curl, const char* url, 
		AbstractCurlResponseHandler* handler);

	enum DownloadFileResult {
		/* Download successful. */
		DOWNLOAD_FILE_OK,
		/* Error initializing curl. */
		DOWNLOAD_FILE_BAD_CURL,
		/* Error creating storage on filesystem. */
		DOWNLOAD_FILE_BAD_FS,
		/* Error while downloading. */
		DOWNLOAD_FILE_DOWNLOAD_ERROR
	};

	/* Download file @file from the url @url. */
	static DownloadFileResult DownloadFile(const char* file, const char* url);

	/* Create the hierarchy of folders required in order to create the file @name. 
	 * 
	 * Return true if folders are created successfully, false otherwise.
	 */
	static bool CreateFileHierarchy(const char* name);

	class CurlStringResponse : public AbstractCurlResponseHandler {
	public:
		size_t OnFirstData(void* data, size_t len, size_t n) {
			return Append(data, len, n);
		}

		size_t OnNewData(void* data, size_t len, size_t n) {
			return Append(data, len, n);
		}

		std::string const& GetData() const {
			return _data;
		}

	private:
		size_t Append(void* data, size_t len, size_t n) {
			_data.append((char*)data, len * n);
			return len * n;
		}

		std::string _data;
	};

	class CurlFileResponse : public AbstractCurlResponseHandler {
	public:
		CurlFileResponse(std::string const& name) {
			_f = fopen(name.c_str(), "wb");
		}

		~CurlFileResponse() {
			if (_f)
				fclose(_f);
		}

		size_t OnFirstData(void* data, size_t len, size_t n) {
			return Append(data, len, n);
		}

		size_t OnNewData(void* data, size_t len, size_t n) {
			return Append(data, len, n);
		}

		FILE* GetFile() const {
			return _f;
		}

	private:
		size_t Append(void* data, size_t len, size_t n) {
			return fwrite(data, len, n, _f);
		}

		FILE* _f = NULL;
	};

	Version const* Updater::GetIsaacVersionFromHash(const char* hash) {
		Version const* version = knownVersions;
		while (version->version) {
			if (!strcmp(hash, version->hash)) {
				return version;
			}

			++version;
		}

		return NULL;
	}

	/* RAII-style class to automatically clean the CURL session. */
	class ScopedCURL {
	public:
		ScopedCURL(CURL* curl) : _curl(curl) { 
		
		}

		~ScopedCURL() {
			if (_curl) {
				curl_easy_cleanup(_curl);
			}
		}

	private:
		CURL* _curl;
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

	RepentogonUpdateState::RepentogonUpdateState() {

	}

	Updater::Updater() {

	}

	bool Updater::FileExists(const char* name) {
		WIN32_FIND_DATAA data;
		return FileExists(name, &data);
	}

	bool Updater::FileExists(const char* name, WIN32_FIND_DATAA* data) {
		memset(data, 0, sizeof(*data));
		HANDLE result = FindFirstFileA(name, data);
		bool ret = result != INVALID_HANDLE_VALUE;
		if (ret) {
			FindClose(result);
		}

		return ret;
	}

	std::string Updater::Sha256F(const char* filename) {
		WIN32_FIND_DATAA data;
		if (!FileExists(filename, &data)) {
			std::ostringstream s;
			s << "Updater::Sha256F: Attempt to hash non existant file " << filename;
			throw std::runtime_error(s.str());
		}

		std::unique_ptr<char[]> content(new char[data.nFileSizeLow]);
		if (!content) {
			std::ostringstream s;
			s << "Updater::Sha256F: Cannot hash " << filename << ": file size (" << data.nFileSizeLow << " bytes) would exceed available memory";
			throw std::runtime_error(s.str());
		}

		FILE* f = fopen(filename, "rb");
		fread(content.get(), data.nFileSizeLow, 1, f);

		return Sha256(content.get(), data.nFileSizeLow);
	}

	std::string Updater::Sha256(const char* str, size_t size) {
		BCRYPT_ALG_HANDLE alg;
		NTSTATUS err = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s; 
			s << "Updater::Sha256: Unable to open BCrypt SHA256 provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		DWORD buffSize;
		DWORD dummy;
		err = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (unsigned char*)&buffSize, sizeof(buffSize), &dummy, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s; 
			s << "Updater::Sha256: Unable to retrieve object length property of BCrypt SHA256 provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		BCRYPT_HASH_HANDLE hashHandle;
		std::unique_ptr<unsigned char[]> hashBuffer(new unsigned char[buffSize]);
		if (!hashBuffer) {
			throw std::runtime_error("Updater::Sha256: Unable to allocate buffer for internal computation");
		}

		err = BCryptCreateHash(alg, &hashHandle, hashBuffer.get(), buffSize, NULL, 0, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to create BCrypt hash object (" << err << ")";
			throw std::runtime_error(s.str());
		}

		err = BCryptHashData(hashHandle, (unsigned char*)str, size, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to hash data (" << err << ")";
			throw std::runtime_error(s.str());
		}

		DWORD hashSize;
		err = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (unsigned char*)&hashSize, sizeof(hashSize), &dummy, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to retrieve hash length property of BCrypt SHA256 provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		std::unique_ptr<unsigned char[]> hash(new unsigned char[hashSize]);
		if (!hash) {
			throw std::runtime_error("Updater::Sha256: Unable to allocate buffer for final computation");
		}

		err = BCryptFinishHash(hashHandle, hash.get(), hashSize, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Unable to finish hashing (" << err << ")";
			throw std::runtime_error(s.str());
		}

		hashBuffer.reset();
		std::unique_ptr<char[]> hashHex(new char[hashSize * 2 + 1]);
		if (!hashHex) {
			throw std::runtime_error("Updater::Sha256: Unable to allocate buffer for hexdump of hash");
		}

		err = BCryptCloseAlgorithmProvider(alg, 0);
		if (!BCRYPT_SUCCESS(err)) {
			std::ostringstream s;
			s << "Updater::Sha256: Error while closing provider (" << err << ")";
			throw std::runtime_error(s.str());
		}

		for (int i = 0; i < hashSize; ++i) {
			sprintf(hashHex.get() + 2 * i, "%02hhx", hash[i]);
		}

		hashHex[hashSize * 2] = '\0';
		/* std::string will perform a copy of the content of the string, 
		 * the unique_ptr can safely release it afterwards. 
		 */
		return std::string(hashHex.get());
	}

	bool Updater::CheckIsaacInstallation() {
		if (!FileExists(isaacExecutable)) {
			return false;
		}

		try {
			std::string hash = Sha256F(isaacExecutable);
			_isaacVersion = GetIsaacVersionFromHash(hash.c_str());
		}
		catch (std::runtime_error& e) {
			Logger::Error("Updater::CheckIsaacInstallation: exception: %s\n", e.what());
		}

		return true;
	}

	bool Updater::CheckRepentogonInstallation() {
		_repentogonFiles.clear();

		const char** mandatoryFile = mandatoryFileNames;
		bool ok = true;
		while (*mandatoryFile) {
			FoundFile& file = _repentogonFiles.emplace_back();
			file.filename = *mandatoryFile;
			file.found = FileExists(*mandatoryFile);

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

		ScoppedModule zhl(LoadLib("libzhl.dll", LOADABLE_DLL_ZHL));
		if (!zhl.Get()) {
			Logger::Error("Updater::CheckRepentogonInstallation: Failed to load libzhl.dll (%d)\n", GetLastError());
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		ScoppedModule repentogon(LoadLib("zhlRepentogon.dll", LOADABLE_DLL_REPENTOGON));
		if (!repentogon.Get()) {
			Logger::Error("Updater::CheckRepentogonInstallation: Failed to load zhlRepentogon.dll (%d)\n", GetLastError());
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		FARPROC zhlVersion = GetProcAddress(zhl.Get(), "__ZHL_VERSION");
		if (!zhlVersion) {
			Logger::Warn("Updater::CheckRepentogonInstallation: libzhl.dll does not export __ZHL_VERSION\n");
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

		FARPROC repentogonVersion = GetProcAddress(repentogon.Get(), "__REPENTOGON_VERSION");
		if (!repentogonVersion) {
			Logger::Warn("Updater::CheckRepentogonInstallation: zhlRepentogon.dll does not export __REPENTOGON_VERSION\n");
			_installationState = REPENTOGON_INSTALLATION_STATE_NONE;
			return false;
		}

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

		if (FileExists("dsound.dll")) {
			_installationState = REPENTOGON_INSTALLATION_STATE_LEGACY;
		}
		else {
			_installationState = REPENTOGON_INSTALLATION_STATE_MODERN;
		}

		return true;
	}

	HMODULE Updater::LoadLib(const char* name, LoadableDlls dll) {
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

	bool Updater::ValidateVersionString(HMODULE module, const char** addr, std::string& version) {
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

	VersionCheckResult Updater::CheckRepentogonUpdates(rapidjson::Document& doc) {
		FetchUpdatesResult fetchResult = FetchRepentogonUpdates(doc);
		if (fetchResult != FETCH_UPDATE_OK) {
			return VERSION_CHECK_ERROR;
		}

		if (_installationState == REPENTOGON_INSTALLATION_STATE_NONE) {
			return VERSION_CHECK_NEW;
		}

		return CheckUpdates(_zhlVersion.c_str(), "Repentogon", doc);
	}

	FetchUpdatesResult Updater::FetchRepentogonUpdates(rapidjson::Document& response) {
		return FetchUpdates(RepentogonURL, response);
	}

	FetchUpdatesResult FetchUpdates(const char* url, rapidjson::Document& response) {
		CURL* curl;
		CURLcode curlResult;

		curl = curl_easy_init();
		if (!curl) {
			Logger::Error("CheckUpdates: error while initializing cURL session for %s\n", url);
			return FETCH_UPDATE_BAD_CURL;
		}

		ScopedCURL session(curl);
		CurlStringResponse data;
		InitCurlSession(curl, url, &data);
		curlResult = curl_easy_perform(curl);
		if (curlResult != CURLE_OK) {
			Logger::Error("CheckUpdates: %s: error while performing HTTP request to retrieve version information: "
				"cURL error: %s", url, curl_easy_strerror(curlResult));
			return FETCH_UPDATE_BAD_REQUEST;
		}

		response.Parse(data.GetData().c_str());

		if (response.HasParseError()) {
			Logger::Error("CheckUpdates: %s: error while parsing HTTP response. rapidjson error code %d", url,
				response.GetParseError());
			return FETCH_UPDATE_BAD_RESPONSE;
		}

		if (!response.HasMember("name")) {
			Logger::Error("CheckUpdates: %s: malformed HTTP response: no field called \"name\"", url);
			return FETCH_UPDATE_NO_NAME;
		}

		return FETCH_UPDATE_OK;
	}

	VersionCheckResult CheckUpdates(const char* installed, const char* tool, 
		rapidjson::Document& response) {
		if (!installed ||
			!strcmp(installed, "nightly") || !strcmp(installed, "dev")) {
			Logger::Info("CheckUpdates: not checking up-to-dateness of %s: dev build found\n", tool);
			return VERSION_CHECK_UTD;
		}

		const char* remoteVersion = response["name"].GetString();
		if (strcmp(remoteVersion, installed)) {
			Logger::Info("CheckUpdates: %s: new version available: %s", tool, remoteVersion);
			return VERSION_CHECK_NEW;
		}
		else {
			Logger::Info("CheckUpdates: %s: up-to-date", tool);
			return VERSION_CHECK_UTD;
		}
	}

	void InitCurlSession(CURL* curl, const char* url, 
		AbstractCurlResponseHandler* handler) {
		struct curl_slist* headers = NULL;
		headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
		headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
		headers = curl_slist_append(headers, "User-Agent: REPENTOGON");

		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AbstractCurlResponseHandler::ResponseSkeleton);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, handler);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, 5);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);
	}

	RepentogonUpdateResult Updater::UpdateRepentogon(rapidjson::Document& data) {
		_repentogonUpdateState.Clear();

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_CHECK_ASSETS;
		if (!CheckRepentogonAssets(data)) {
			Logger::Error("Updater::UpdateRepentogon: invalid assets\n");
			return REPENTOGON_UPDATE_RESULT_MISSING_ASSET;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_DOWNLOAD;
		if (!DownloadRepentogon()) {
			Logger::Error("Updater::UpdateRepentogon: download failed\n");
			return REPENTOGON_UPDATE_RESULT_DOWNLOAD_ERROR;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_CHECK_HASH;
		if (!CheckRepentogonIntegrity()) {
			Logger::Error("Updater::UpdateRepentogon: invalid zip\n");
			return REPENTOGON_UPDATE_RESULT_BAD_HASH;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_UNPACK;
		if (!ExtractRepentogon()) {
			Logger::Error("Updater::UpdateRepentogon: extraction failed\n");
			return REPENTOGON_UPDATE_RESULT_EXTRACT_FAILED;
		}

		_repentogonUpdateState.phase = REPENTOGON_UPDATE_PHASE_DONE;
		return REPENTOGON_UPDATE_RESULT_OK;
	}

	bool Updater::CheckRepentogonAssets(rapidjson::Document const& data) {
		if (!data.HasMember("assets")) {
			Logger::Error("Updater::CheckRepentogonAssets: no \"assets\" field\n");
			return false;
		}

		if (!data["assets"].IsArray()) {
			Logger::Error("Updater::CheckRepentogonAssets: \"assets\" field is not an array\n");
			return false;
		}

		rapidjson::GenericArray<true, rapidjson::Value> const& assets = data["assets"].GetArray();
		if (assets.Size() < 2) {
			Logger::Error("Updater::CheckRepentogonAssets: not enough assets\n");
			return false;
		}

		bool hasHash = false, hasZip = false;
		for (int i = 0; i < assets.Size() && (!hasHash || !hasZip); ++i) {
			if (!assets[i].IsObject())
				continue;

#define _GetObject GetObject
#undef GetObject
			rapidjson::GenericObject<true, rapidjson::Value> const& asset = assets[i].GetObject();
#define GetObject _GetObject
			if (!asset.HasMember("name") || !asset["name"].IsString())
				continue;

			const char* name = asset["name"].GetString();
			Logger::Info("Updater::CheckRepentogonAssets: asset %d -> %s\n", i, name);
			bool isHash = !strcmp(name, HashName);
			bool isRepentogon = !strcmp(name, RepentogonZipName);
			if (!isHash && !isRepentogon)
				continue;

			if (!asset.HasMember("browser_download_url") || !asset["browser_download_url"].IsString()) {
				Logger::Warn("Updater::CheckRepentogonAssets: asset %s has no /invalid \"browser_download_url\" field\n");
				continue;
			}

			const char* url = asset["browser_download_url"].GetString();
			if (isHash) {
				_repentogonUpdateState.hashOk = true;
				_repentogonUpdateState.hashUrl = url;
				hasHash = true;
			}
			else if (isRepentogon) {
				_repentogonUpdateState.zipOk = true;
				_repentogonUpdateState.zipUrl = url;
				hasZip = true;
			}
		}

		if (!hasHash) {
			Logger::Error("Updater::CheckRepentogonAssets: missing hash\n");
		}

		if (!hasZip) {
			Logger::Error("Updater::CheckRepentogonAssets: missing zip\n");
		}

		return hasHash && hasZip;
	}

	bool Updater::DownloadRepentogon() {
		if (DownloadFile(HashName, _repentogonUpdateState.hashUrl.c_str()) != DOWNLOAD_FILE_OK) {
			Logger::Error("Updater::DownloadRepentogon: error while downloading hash\n");
			return false;
		}

		_repentogonUpdateState.hashFile = fopen(HashName, "r");
		if (!_repentogonUpdateState.hashFile) {
			Logger::Error("Updater::DownloadRepentogon: Unable to open %s for reading\n", HashName);
			return false;
		}

		if (DownloadFile(RepentogonZipName, _repentogonUpdateState.zipUrl.c_str()) != DOWNLOAD_FILE_OK) {
			Logger::Error("Updater::DownloadRepentogon: error while downloading zip\n");
			return false;
		}

		_repentogonUpdateState.zipFile = fopen(RepentogonZipName, "r");
		if (!_repentogonUpdateState.zipFile) {
			Logger::Error("Updater::DownloadRepentogon: Unable to open %s for reading\n", RepentogonZipName);
			return false;
		}

		return true;
	}

	bool Updater::CheckRepentogonIntegrity() {
		if (!_repentogonUpdateState.hashFile || !_repentogonUpdateState.zipFile) {
			Logger::Error("Updater::CheckRepentogonIntegrity: hash or archive not previously opened\n");
			return false;
		}

		char hash[4096];
		char* result = fgets(hash, 4096, _repentogonUpdateState.hashFile);

		if (!result) {
			Logger::Error("Updater::CheckRepentogonIntegrity: fgets error\n");
			return false;
		}

		if (!feof(_repentogonUpdateState.hashFile)) {
			/* If EOF is not encountered, then there must be a newline character,
			 * otherwise the file is malformed.
			 */
			char* nl = strchr(hash, '\n');
			if (!nl) {
				Logger::Error("Updater::CheckRepentogonIntegrity: no newline found, malformed hash\n");
				return false;
			}

			*nl = '\0';
		}

		size_t len = strlen(hash);
		if (len != 64) {
			Logger::Error("Updater::CheckRepentogonIntegrity: hash is not 64 characters\n");
			return false;
		}

		std::string zipHash;
		try {
			zipHash = Sha256F(RepentogonZipName);
		}
		catch (std::exception& e) {
			Logger::Fatal("Updater::CHeckRepentogonIntegrity: exception while hashing %s: %s\n", RepentogonZipName, e.what());
			throw;
		}

		std::transform(zipHash.begin(), zipHash.end(), zipHash.begin(), ::toupper);
		std::transform(hash, hash + len, hash, ::toupper);

		_repentogonUpdateState.hash = hash;
		_repentogonUpdateState.zipHash = zipHash;

		if (strcmp(hash, zipHash.c_str())) {
			Logger::Error("Updater::CheckRepentogonIntegrity: hash mismatch: expected \"%s\", got \"%s\"\n", zipHash.c_str(), hash);
			return false;
		}

		return true;
	}

	bool Updater::ExtractRepentogon() {
		std::vector<std::tuple<std::string, bool>>& filesState = _repentogonUpdateState.unzipedFiles;
		int error;
		zip_t* zip = zip_open(RepentogonZipName, ZIP_RDONLY | ZIP_CHECKCONS, &error);
		if (!zip) {
			Logger::Error("Updater::ExtractRepentogon: error %d while opening %s\n", error, RepentogonZipName);
			return false;
		}

		bool ok = true;
		int nFiles = zip_get_num_entries(zip, 0);
		for (int i = 0; i < nFiles; ++i) {
			zip_file_t* file = zip_fopen_index(zip, i, 0);
			if (!file) {
				Logger::Error("Updater::ExtractRepentogon: error opening file %d in archive\n", i);
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple("", false));
				continue;
			}

			const char* name = zip_get_name(zip, i, 0);
			if (!name) {
				zip_error_t* error = zip_get_error(zip);
				Logger::Error("Updater::ExtractRepentogon: error getting name of file %d in archive (%s)\n", i, zip_error_strerror(error));
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple("", false));
				continue;
			}

			if (!CreateFileHierarchy(name)) {
				Logger::Error("Updater::ExtractRepentogon: cannot create intermediate folders for file %s\n", name);
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple(name, false));
				continue;
			}

			FILE* output = fopen(name, "wb");
			if (!output) {
				Logger::Error("Updater::ExtractRepentogon: cannot create file %s\n", name);
				ok = false;
				zip_fclose(file);
				filesState.push_back(std::make_tuple(name, false));
				continue;
			}

			char buffer[4096];
			int count = zip_fread(file, buffer, 4096);
			bool fileOk = true;
			while (count != 0) {
				if (count == -1) {
					Logger::Error("Updater::ExtractRepentogon: error while reading %s\n", name);
					ok = false;
					fileOk = false;
					break;
				}

				fwrite(buffer, count, 1, output);
				count = zip_fread(file, buffer, 4096);
			}

			filesState.push_back(std::make_tuple(name, fileOk));
			fclose(output);
			zip_fclose(file);
		}

		zip_close(zip);
		return ok;
	}

	DownloadFileResult DownloadFile(const char* file, const char* url) {
		CurlFileResponse response(file);
		if (!response.GetFile()) {
			Logger::Error("DownloadFile: unable to open %s for writing\n", file);
			return DOWNLOAD_FILE_BAD_FS;
		}

		CURL* curl = curl_easy_init();
		CURLcode code;

		if (!curl) {
			Logger::Error("DownloadFile: error while initializing curl session to download hash\n");
			return DOWNLOAD_FILE_BAD_CURL;
		}

		ScopedCURL scoppedCurl(curl);
		InitCurlSession(curl, url, &response);
		code = curl_easy_perform(curl);
		if (code != CURLE_OK) {
			Logger::Error("DownloadFile: error while downloading hash: %s\n", curl_easy_strerror(code));
			return DOWNLOAD_FILE_DOWNLOAD_ERROR;
		}

		return DOWNLOAD_FILE_OK;
	}

	VersionCheckResult Updater::CheckLauncherUpdates(rapidjson::Document&) {
		return VERSION_CHECK_UTD;
	}

	RepentogonInstallationState Updater::GetRepentogonInstallationState() const {
		return _installationState;
	}

	Version const* Updater::GetIsaacVersion() const {
		return _isaacVersion;
	}

	bool Updater::WasLibraryLoaded(LoadableDlls dll) const {
		return _dllStates[dll];
	}

	std::vector<FoundFile> const& Updater::GetRepentogonInstallationFilesState() const {
		return _repentogonFiles;
	}

	std::string const& Updater::GetRepentogonVersion() const {
		return _repentogonVersion;
	}

	std::string const& Updater::GetZHLVersion() const {
		return _zhlVersion;
	}

	bool Updater::RepentogonZHLVersionMatch() const {
		return _repentogonZHLVersionMatch;
	}

	RepentogonUpdateState const& Updater::GetRepentogonUpdateState() const {
		return _repentogonUpdateState;
	}

	void RepentogonUpdateState::Clear() {
		phase = REPENTOGON_UPDATE_PHASE_CHECK_ASSETS;
		zipOk = hashOk = false;
		hashUrl.clear();
		zipUrl.clear();
		hashFile = (FILE*)NULL;
		zipFile = (FILE*)NULL;
		hash.clear();
		zipHash.clear();
		unzipedFiles.clear();
	}

	bool CreateFileHierarchy(const char* name) {
		char* copy = (char*)malloc(strlen(name) + 1);
		if (!copy) {
			Logger::Error("CreateFileHierarchy: unable to allocate memory to duplicate %s\n", name);
			return false;
		}

		strcpy(copy, name);
		char* next = strpbrk(copy, "/");
		while (next) {
			char save = *next;
			*next = '\0';
			BOOL created = CreateDirectoryA(copy, NULL);
			if (!created) {
				DWORD lastError = GetLastError();
				if (lastError != ERROR_ALREADY_EXISTS) {
					Logger::Error("CreateFileHierarchy: unable to create folder %s: %d\n", copy, lastError);
					free(copy);
					return false;
				}
			}
			*next = save;
			next = strpbrk(next + 1, "/");
		}

		free(copy);
		return true;
	}
}