#include <curl/curl.h>

#include "shared/github.h"
#include "shared/logger.h"
#include "shared/scoped_curl.h"
#include "shared/curl/file_response_handler.h"
#include "shared/curl/string_response_handler.h"

namespace Github {
	static void MonitorNotifyOnDataReceived(Threading::Monitor<GithubDownloadNotification>* monitor, bool first,
		void* data, size_t n, size_t count);

	static GithubDownloadNotification CreateNotification(GithubDownloadNotificationType type);
	static GithubDownloadNotification CreateInitCurlNotification(const char* url, bool done);
	static GithubDownloadNotification CreatePerformCurlNotification(const char* url, bool done);
	static GithubDownloadNotification CreateDoneNotification(const char* url);
	static GithubDownloadNotification CreateParseResponseNotification(const char* url, bool done);

	DownloadAsStringResult DownloadAsString(const char* url, std::string& response,
		Threading::Monitor<GithubDownloadNotification>* monitor) {
		CURL* curl;
		CURLcode curlResult;

		CurlStringResponse data;

		if (monitor) {
			data.RegisterHook(std::bind_front(MonitorNotifyOnDataReceived, monitor));
			monitor->Push(CreateInitCurlNotification(url, false));
		}

		curl = curl_easy_init();
		if (!curl) {
			Logger::Error("CheckUpdates: error while initializing cURL session for %s\n", url);
			return DOWNLOAD_AS_STRING_BAD_CURL;
		}

		ScopedCURL session(curl);
		InitCurlSession(curl, url, &data);

		if (monitor) {
			monitor->Push(CreateInitCurlNotification(url, true));
			monitor->Push(CreatePerformCurlNotification(url, false));
		}

		curlResult = curl_easy_perform(curl);
		if (curlResult != CURLE_OK) {
			Logger::Error("CheckUpdates: %s: error while performing HTTP request to retrieve version information: "
				"cURL error: %s", url, curl_easy_strerror(curlResult));
			return DOWNLOAD_AS_STRING_BAD_REQUEST;
		}

		if (monitor) {
			monitor->Push(CreatePerformCurlNotification(url, true));
			monitor->Push(CreateDoneNotification(url));
		}

		response = data.GetData();
		return DOWNLOAD_AS_STRING_OK;
	}

	DownloadAsStringResult FetchReleaseInfo(const char* url,
		rapidjson::Document& response,
		Threading::Monitor<GithubDownloadNotification>* monitor) {
		std::string stringResponse;
		DownloadAsStringResult result = DownloadAsString(url, stringResponse, monitor);
		if (result != DOWNLOAD_AS_STRING_OK) {
			return result;
		}

		if (monitor)
			monitor->Push(CreateParseResponseNotification(url, false));

		response.Parse(stringResponse.c_str());

		if (monitor)
			monitor->Push(CreateParseResponseNotification(url, true));

		if (response.HasParseError()) {
			Logger::Error("CheckUpdates: %s: error while parsing HTTP response. rapidjson error code %d", url,
				response.GetParseError());
			return Github::DOWNLOAD_AS_STRING_INVALID_JSON;
		}

		if (!response.HasMember("name")) {
			Logger::Error("CheckUpdates: %s: malformed HTTP response: no field called \"name\"", url);
			return Github::DOWNLOAD_AS_STRING_NO_NAME;
		}

		return DOWNLOAD_AS_STRING_OK;
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

	DownloadFileResult DownloadFile(const char* file, const char* url,
		Threading::Monitor<GithubDownloadNotification>* monitor) {
		CurlFileResponse response(file);
		
		if (!response.GetFile()) {
			Logger::Error("DownloadFile: unable to open %s for writing\n", file);
			return DOWNLOAD_FILE_BAD_FS;
		}

		if (monitor) {
			response.RegisterHook(std::bind_front(MonitorNotifyOnDataReceived, monitor));
			monitor->Push(CreateInitCurlNotification(url, false));
		}

		CURL* curl = curl_easy_init();
		CURLcode code;

		if (!curl) {
			Logger::Error("DownloadFile: error while initializing curl session to download hash\n");
			return DOWNLOAD_FILE_BAD_CURL;
		}

		ScopedCURL scoppedCurl(curl);
		InitCurlSession(curl, url, &response);

		if (monitor) {
			monitor->Push(CreateInitCurlNotification(url, true));
			monitor->Push(CreatePerformCurlNotification(url, false));
		}

		code = curl_easy_perform(curl);
		if (code != CURLE_OK) {
			Logger::Error("DownloadFile: error while downloading hash: %s\n", curl_easy_strerror(code));
			return DOWNLOAD_FILE_DOWNLOAD_ERROR;
		}

		if (monitor) {
			monitor->Push(CreatePerformCurlNotification(url, true));
			monitor->Push(CreateDoneNotification(url));
		}

		return DOWNLOAD_FILE_OK;
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

	void MonitorNotifyOnDataReceived(Threading::Monitor<GithubDownloadNotification>* monitor, bool first,
		void* data, size_t n, size_t count) {
		GithubDownloadNotification notification;
		notification.type = GH_NOTIFICATION_DATA_RECEIVED;
		notification.data = n * count;
		monitor->Push(notification);
	}

	GithubDownloadNotification CreateNotification(GithubDownloadNotificationType type) {
		GithubDownloadNotification notification;
		notification.type = type;
		return notification;
	}

	GithubDownloadNotification CreateInitCurlNotification(const char* url, bool done) {
		GithubDownloadNotification notification;
		notification.type = done ? GH_NOTIFICATION_INIT_CURL_DONE : GH_NOTIFICATION_INIT_CURL;
		notification.data = url;
		return notification;
	}

	GithubDownloadNotification CreatePerformCurlNotification(const char* url, bool done) {
		GithubDownloadNotification notification;
		notification.type = done ? GH_NOTIFICATION_CURL_PERFORM_DONE : GH_NOTIFICATION_CURL_PERFORM;
		notification.data = url;
		return notification;
	}

	GithubDownloadNotification CreateParseResponseNotification(const char* url, bool done) {
		GithubDownloadNotification notification;
		notification.type = done ? GH_NOTIFICATION_PARSE_RESPONSE_DONE : GH_NOTIFICATION_PARSE_RESPONSE;
		notification.data = url;
		return notification;
	}

	GithubDownloadNotification CreateDoneNotification(const char* url) {
		GithubDownloadNotification notification;
		notification.type = GH_NOTIFICATION_DONE;
		notification.data = url;
		return notification;
	}
}