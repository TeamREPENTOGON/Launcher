#pragma once

#include <variant>

#include "rapidjson/document.h"

#include "shared/monitor.h"
#include "shared/curl/abstract_response_handler.h"

namespace Github {
	/* Result of fetching updates. */
	enum FetchUpdatesResult {
		/* Fetched successfully. */
		FETCH_UPDATE_OK,
		/* cURL error. */
		FETCH_UPDATE_BAD_CURL,
		/* Invalid URL. */
		FETCH_UPDATE_BAD_REQUEST,
		/* Download error. */
		FETCH_UPDATE_BAD_RESPONSE,
		/* Response has no "name" field. */
		FETCH_UPDATE_NO_NAME
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
	};

	/* Check if the latest release data is newer than the installed version.
	 *
	 * tool is the name of the tool whose version is checked, installedVersion
	 * is the version of this tool, document is the JSON of the latest release
	 * of said tool.
	 */
	VersionCheckResult CheckUpdates(const char* installedVersion,
		const char* tool, rapidjson::Document& document);

	/* Fetch the content at url and store it in response.
	 *
	 * The content is assumed to be the JSON of a GitHub release and as such
	 * is expected to have a "name" field.
	 */
	FetchUpdatesResult FetchUpdates(const char* url,
		rapidjson::Document& response,
		Threading::Monitor<GithubDownloadNotification>* monitor);

	/* Download file @file from the url @url. */
	DownloadFileResult DownloadFile(const char* file, const char* url,
		Threading::Monitor<GithubDownloadNotification>* monitor);

	/* Initialize a cURL session with sane parameters.
	 *
	 * url is the target address. handler is an instance of the generic class
	 * used to process the data received.
	 */
	void InitCurlSession(CURL* curl, const char* url,
		AbstractCurlResponseHandler* handler);
}