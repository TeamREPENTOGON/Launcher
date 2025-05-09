#pragma once

#include <variant>

#include "curl/curl.h"

#include "rapidjson/document.h"

#include "shared/curl_request.h"
#include "shared/monitor.h"
#include "shared/curl/abstract_response_handler.h"

namespace Github {
	/* Result of comparing installed version with latest release. */
	enum VersionCheckResult {
		/* New version available. */
		VERSION_CHECK_NEW,
		/* Up-to-date. */
		VERSION_CHECK_UTD,
		/* Error while checking version. */
		VERSION_CHECK_ERROR
	};

	enum ReleaseInfoResult {
		RELEASE_INFO_OK,
		RELEASE_INFO_CURL_ERROR,
		RELEASE_INFO_JSON_ERROR,
		RELEASE_INFO_NO_NAME
	};

	void GenerateGithubHeaders(curl::RequestParameters& parameters);

	/* Check if the latest release data is newer than the installed version.
	 *
	 * tool is the name of the tool whose version is checked, installedVersion
	 * is the version of this tool, document is the JSON of the latest release
	 * of said tool.
	 */
	VersionCheckResult CheckUpdates(const char* installedVersion,
		const char* tool, rapidjson::Document& document);

	/* Fetch the content at the url in the request, assuming it to describe a
	 * release, and store it in response.
	 *
	 * The release is well formed if it is JSON formatted and contains a "name"
	 * field.
	 */
	ReleaseInfoResult FetchReleaseInfo(curl::RequestParameters const& request,
		rapidjson::Document& response, curl::DownloadStringResult* curlResult,
		std::string name);

	/* Asynchronously fetch the content of the url in the request, assuming it
	 * to describe a release. Return a descriptor used to synchronize with the
	 * completion of the transfer.
	 *
	 * Call ValidateReleaseInfo to check whether the release is well formed or
	 * not.
	 */
	std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> AsyncFetchReleaseInfo(
		curl::RequestParameters const& request
	);

	/* Validate the result of (Async)FetchReleaseInfo. */
	ReleaseInfoResult ValidateReleaseInfo(curl::DownloadStringDescriptor const& desc,
		rapidjson::Document& response, curl::DownloadStringResult* curlResult);
}