#include "shared/curl_request.h"
#include "shared/github_executor.h"
#include "shared/logger.h"
#include "shared/scoped_curl.h"
#include "shared/curl/string_response_handler.h"
#include "shared/private/curl/curl_request.h"

namespace curl {
	std::shared_ptr<AsynchronousDownloadFileDescriptor> AsyncDownloadFile(
		RequestParameters const& parameters, std::string filename) {
		std::shared_ptr<AsynchronousDownloadFileDescriptor> descriptor =
			std::make_shared<AsynchronousDownloadFileDescriptor>();
		descriptor->result = sGithubExecutor->AddDownloadFileRequest(
			std::move(filename), parameters, descriptor);
		return descriptor;
	}

	std::shared_ptr<AsynchronousDownloadStringDescriptor> AsyncDownloadString(
		RequestParameters const& parameters, std::string name) {
		std::shared_ptr<AsynchronousDownloadStringDescriptor> descriptor =
			std::make_shared<AsynchronousDownloadStringDescriptor>();
		descriptor->result = sGithubExecutor->AddDownloadStringRequest(
			std::move(name), parameters, descriptor);
		return descriptor;
	}

	DownloadStringDescriptor DownloadString(RequestParameters const& parameters,
		std::string const& name) {
		return curl::detail::DownloadString(parameters, name, nullptr);
	}

	DownloadFileDescriptor DownloadFile(RequestParameters const& parameters,
		std::string const& filename) {
		return curl::detail::DownloadFile(parameters, filename, nullptr);
	}

	const char* DownloadAsStringResultToLogString(DownloadStringResult result) {
		switch (result) {
		case DOWNLOAD_STRING_OK:
			return "Successfully downloaded";

		case DOWNLOAD_STRING_BAD_CURL:
			return "Error while initiating cURL connection";

		case DOWNLOAD_STRING_BAD_REQUEST:
			return "Error while performing cURL request";

		default:
			sprintf(_downloadAsStringResultToLogStringBuffer, "Unexpected error %d: Please report this as a bug", result);
			return _downloadAsStringResultToLogStringBuffer;
		}
	}
}