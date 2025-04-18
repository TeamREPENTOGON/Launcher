#pragma once

#include <variant>

#include "curl/curl.h"

#include "rapidjson/document.h"

#include "shared/curl_request.h"
#include "shared/monitor.h"
#include "shared/curl/abstract_response_handler.h"

namespace Github {
	static char _downloadAsStringResultToLogStringBuffer[128];

	/* Result of fetching updates. */
	enum DownloadAsStringResult {
		/* Fetched successfully. */
		DOWNLOAD_AS_STRING_OK,
		/* cURL error. */
		DOWNLOAD_AS_STRING_BAD_CURL,
		/* Invalid URL. */
		DOWNLOAD_AS_STRING_BAD_REQUEST,
		/* Download error: invalid JSON. */
		DOWNLOAD_AS_STRING_INVALID_JSON,
		/* Response has no "name" field. */
		DOWNLOAD_AS_STRING_NO_NAME
	};

	/* Result of comparing installed version with latest release. */
	enum VersionCheckResult {
		/* New version available. */
		VERSION_CHECK_NEW,
		/* Up-to-date. */
		VERSION_CHECK_UTD,
		/* Error while checking version. */
		VERSION_CHECK_ERROR
	};

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

	enum GithubDownloadNotificationType {
		/* End of message stream. */
		GH_NOTIFICATION_DONE,
		GH_NOTIFICATION_INIT_CURL,
		GH_NOTIFICATION_INIT_CURL_DONE,
		GH_NOTIFICATION_CURL_PERFORM,
		GH_NOTIFICATION_CURL_PERFORM_DONE,
		GH_NOTIFICATION_PARSE_RESPONSE,
		GH_NOTIFICATION_PARSE_RESPONSE_DONE,
		GH_NOTIFICATION_DATA_RECEIVED,
	};

	struct GithubDownloadNotification {
		GithubDownloadNotificationType type;
		std::variant<std::string, size_t> data;
		std::string name;
		uint32_t id;
	};

	typedef Threading::Monitor<GithubDownloadNotification> DownloadMonitor;

	/* Check if the latest release data is newer than the installed version.
	 *
	 * tool is the name of the tool whose version is checked, installedVersion
	 * is the version of this tool, document is the JSON of the latest release
	 * of said tool.
	 */
	VersionCheckResult CheckUpdates(const char* installedVersion,
		const char* tool, rapidjson::Document& document);

	/* Fetch the content at the url in the request and store it in response.
	 *
	 * @a name is a symbolic name used for logging purposes.
	 *
	 * This function cannot return the DOWNLOAD_AS_STRING_NO_NAME or
	 * DOWNLOAD_AS_STRING_INVALID_JSON error codes.
	 */
	DownloadAsStringResult DownloadAsString(CURLRequest const& request,
		const char* name, std::string& response, DownloadMonitor* monitor);

	/* Fetch the content at the url in the request, assuming it to describe a
	 * release, and store it in response.
	 *
	 * The release is well formed if it is JSON formatted and contains a "name"
	 * field.
	 */
	DownloadAsStringResult FetchReleaseInfo(CURLRequest const& request,
		rapidjson::Document& response, DownloadMonitor* monitor);

	/* Download file @file from the url in the request. */
	DownloadFileResult DownloadFile(const char* file, CURLRequest const& request,
		DownloadMonitor* monitor);

	/* Initialize a cURL session with sane parameters.
	 *
	 * url is stored in the request. handler is an instance of the generic class
	 * used to process the data received. Other parameters in the request are
	 * given to the appropriate curl options.
	 */
	void InitCurlSession(CURL* curl, CURLRequest const& request,
		AbstractCurlResponseHandler* handler);

	/* Returns a human readable description of a DownloadAsStringResult for use in logging. */
	const char* DownloadAsStringResultToLogString(DownloadAsStringResult result);
}