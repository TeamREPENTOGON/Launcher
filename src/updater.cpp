#include <Windows.h>
#include <bcrypt.h>
#include <Psapi.h>

#include <memory>
#include <sstream>
#include <stdexcept>

#include <curl/curl.h>

#include "shared/curl/abstract_response_handler.h"
#include "shared/curl/file_response_handler.h"
#include "shared/curl/string_response_handler.h"
#include "launcher/filesystem.h"
#include "shared/logger.h"
#include "launcher/updater.h"

#include "zip.h"

namespace Launcher {
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

	RepentogonUpdateState::RepentogonUpdateState() {

	}

	Updater::Updater() {

	}

	VersionCheckResult Updater::CheckRepentogonUpdates(rapidjson::Document& doc, 
		fs::Installation const& installation) {
		FetchUpdatesResult fetchResult = FetchRepentogonUpdates(doc);
		if (fetchResult != FETCH_UPDATE_OK) {
			return VERSION_CHECK_ERROR;
		}

		if (installation.GetRepentogonInstallationState() == fs::REPENTOGON_INSTALLATION_STATE_NONE) {
			return VERSION_CHECK_NEW;
		}

		return CheckUpdates(installation.GetZHLVersion().c_str(), "Repentogon", doc);
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
			zipHash = fs::Sha256F(RepentogonZipName);
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