#include <atomic>

#include <curl/curl.h>
#include <sstream>

#include "shared/filesystem.h"
#include "shared/github.h"
#include "shared/logger.h"
#include "shared/scoped_curl.h"
#include "shared/curl/file_response_handler.h"
#include "shared/curl/string_response_handler.h"
#include "shared/loggable_gui.h"


namespace Github {
	static std::atomic<uint32_t> __gitDownloadCounter = 0;

	ReleaseInfoResult FetchReleaseInfo(curl::RequestParameters const& request,
		rapidjson::Document& response, curl::DownloadStringResult* curlResult,
		std::string name) {
		curl::DownloadStringDescriptor descriptor = curl::DownloadString(request, std::move(name));
		return ValidateReleaseInfo(descriptor, response, curlResult);
	}

	std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> AsyncFetchReleaseInfo(
		curl::RequestParameters const& request) {
		return curl::AsyncDownloadString(request, "release info");
	}

	ReleaseInfoResult ValidateReleaseInfo(curl::DownloadStringDescriptor const& desc,
		rapidjson::Document& response, curl::DownloadStringResult* curlResult) {
		if (curlResult) {
			*curlResult = desc.result;
		}

		if (desc.result != curl::DOWNLOAD_STRING_OK) {
			return RELEASE_INFO_CURL_ERROR;
		}

		rapidjson::Document document;
		document.Parse(desc.string.c_str());
		if (document.HasParseError()) {
			return RELEASE_INFO_JSON_ERROR;
		}

		if (!document.HasMember("name")) {
			return RELEASE_INFO_NO_NAME;
		}

		response = std::move(document);
		return RELEASE_INFO_OK;
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
			Logger::Info("CheckUpdates: %s: new version available: '%s'\n", tool, remoteVersion);
			return VERSION_CHECK_NEW;
		} else {
			Logger::Info("CheckUpdates: %s: up-to-date\n", tool);
			return VERSION_CHECK_UTD;
		}
	}

	void GenerateGithubHeaders(curl::RequestParameters& parameters) {
		parameters.headers.push_back("Accept: application/vnd.github+json");
		parameters.headers.push_back("X-GitHub-Api-Version: 2022-11-28");
		parameters.headers.push_back("User-Agent: REPENTOGON");

		const char* file = "github.token";
		if (Filesystem::Exists(file)) {
			FILE* f = fopen(file, "r");
			long begin = ftell(f);
			fseek(f, 0, SEEK_END);
			long end = ftell(f);
			fseek(f, begin, SEEK_SET);

			const char* base = "Authorization: Bearer ";
			size_t baseLen = strlen(base);
			size_t len = baseLen + end - begin + 1;
			char* buffer = (char*)malloc(len);
			sprintf(buffer, "%s", base);
			fread(buffer + baseLen, 1, end - begin, f);
			buffer[len - 1] = '\0';
			fclose(f);
			Logger::Info("Adding Authorization header: %s\n", buffer);
			parameters.headers.push_back(buffer);
			free(buffer);
		}
	}
}