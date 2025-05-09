#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include <curl/curl.h>

#include "shared/curl/file_response_handler.h"
#include "shared/monitor.h"

namespace curl {
	static char _downloadAsStringResultToLogStringBuffer[128];

	enum DownloadStringResult {
		/* Fetched successfully. */
		DOWNLOAD_STRING_OK,
		/* cURL error. */
		DOWNLOAD_STRING_BAD_CURL,
		/* Invalid URL. */
		DOWNLOAD_STRING_BAD_REQUEST
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

	enum DownloadNotificationType {
		/* End of message stream. */
		DOWNLOAD_DONE,
		DOWNLOAD_ABORTED,
		DOWNLOAD_INIT_CURL,
		DOWNLOAD_INIT_CURL_DONE,
		DOWNLOAD_CURL_PERFORM,
		DOWNLOAD_CURL_PERFORM_DONE,
		DOWNLOAD_DATA_RECEIVED
	};

	struct DownloadNotification {
		DownloadNotificationType			type;
		std::variant<std::string, size_t>	data;
		std::string							name;
		uint32_t							id;
	};

	typedef Threading::Monitor<DownloadNotification> DownloadMonitor;

	struct DownloadStringDescriptor {
		DownloadStringResult	result;
		CURLcode				code;
		std::string				string;
	};

	struct DownloadFileDescriptor {
		DownloadFileResult	result;
		CURLcode			code;
		std::string			filename;
	};

	struct AsynchronousDownloadDescriptor {
		Threading::Monitor<DownloadNotification>	monitor;
		std::atomic<bool>							cancel;
		std::atomic<bool>							pause;
		std::string									name;
	};

	struct AsynchronousDownloadStringDescriptor {
		AsynchronousDownloadDescriptor			base;
		std::future<DownloadStringDescriptor>	result;
	};

	struct AsynchronousDownloadFileDescriptor {
		AsynchronousDownloadDescriptor			base;
		std::future<DownloadFileDescriptor>		result;
	};

	struct RequestParameters {
		std::string					url;
		long						timeout = 0;
		long						serverTimeout = 0;
		curl_off_t					maxSpeed = 0;
		std::vector<std::string>	headers;
	};

	/* Returns a human readable description of a DownloadAsStringResult for use in logging. */
	const char* DownloadAsStringResultToLogString(DownloadStringResult result);

	std::shared_ptr<AsynchronousDownloadStringDescriptor> AsyncDownloadString(
		RequestParameters const& parameters, std::string name);

	std::shared_ptr<AsynchronousDownloadFileDescriptor> AsyncDownloadFile(
		RequestParameters const& parameters, std::string filename);

	DownloadStringDescriptor DownloadString(RequestParameters const& parameters,
		std::string const& name);

	DownloadFileDescriptor DownloadFile(RequestParameters const& parameters,
		std::string const& filename);
}