#pragma once

#include "shared/curl_request.h"
#include "shared/logger.h"
#include "shared/scoped_curl.h"
#include "shared/curl/string_response_handler.h"
#include "shared/private/curl/curl_request.h"

namespace curl::detail {
	static bool MonitorNotifyOnDataReceived(Threading::Monitor<DownloadNotification>* monitor,
		const char* name, std::atomic<bool>* cancel, uint32_t id, bool first,
		void* data, size_t n, size_t count);

	static DownloadNotification CreateNotification(DownloadNotificationType type);
	static DownloadNotification CreateInitCurlNotification(const char* url, bool done);
	static DownloadNotification CreatePerformCurlNotification(const char* url, bool done);
	static DownloadNotification CreateDoneNotification(const char* url);

	static std::atomic<uint32_t> __downloadCounter = 0;

	void InitCurlSession(CURL* curl, RequestParameters const& request,
		AbstractCurlResponseHandler* handler);

	DownloadStringDescriptor DownloadString(RequestParameters const& parameters,
		std::string const& name,
		std::shared_ptr<AsynchronousDownloadStringDescriptor> const& desc) {
		const char* url = parameters.url.c_str();

		CurlStringResponse data;
		uint32_t id = __downloadCounter.fetch_add(1, std::memory_order_acq_rel) + 1;

		Threading::Monitor<DownloadNotification>* monitor = desc ? &desc->base.monitor : nullptr;
		std::atomic<bool>* cancelFlag = desc ? &desc->base.cancel : nullptr;
		if (monitor) {
			data.RegisterHook(std::bind_front(MonitorNotifyOnDataReceived, monitor, name.c_str(), cancelFlag, id));
			monitor->Push(CreateInitCurlNotification(url, false));
		}

		CURL* curl = curl_easy_init();
		DownloadStringDescriptor result;
		if (!curl) {
			Logger::Error("DownloadAsString: error while initializing cURL session for %s\n", url);
			result.result = DOWNLOAD_STRING_BAD_CURL;
			return result;
		}

		ScopedCURL session(curl);
		InitCurlSession(curl, parameters, &data);

		if (monitor) {
			monitor->Push(CreateInitCurlNotification(url, true));
			monitor->Push(CreatePerformCurlNotification(url, false));
		}

		result.code = curl_easy_perform(curl);
		if (result.code != CURLE_OK) {
			Logger::Error("DownloadAsString: %s: error while performing HTTP request: "
				"cURL error: %s", url, curl_easy_strerror(result.code));
			result.result = DOWNLOAD_STRING_BAD_REQUEST;
			return result;
		}

		if (monitor) {
			monitor->Push(CreatePerformCurlNotification(url, true));
			monitor->Push(CreateDoneNotification(url));
		}

		result.string = data.GetData();
		result.result = DOWNLOAD_STRING_OK;
		return result;
	}

	DownloadFileDescriptor DownloadFile(RequestParameters const& parameters,
		std::string const& filename,
		std::shared_ptr<AsynchronousDownloadFileDescriptor> const& desc) {
		const char* url = parameters.url.c_str();
		CurlFileResponse response(filename);

		DownloadFileDescriptor result;
		result.filename = filename;

		if (!response.GetFile()) {
			Logger::Error("DownloadFile: unable to open %s for writing\n", filename.c_str());
			result.result = DOWNLOAD_FILE_BAD_FS;
			return result;
		}

		uint32_t id = ++__downloadCounter;
		Threading::Monitor<DownloadNotification>* monitor = desc ? &desc->base.monitor : nullptr;
		std::atomic<bool>* cancelFlag = desc ? &desc->base.cancel : nullptr;
		if (monitor) {
			response.RegisterHook(std::bind_front(MonitorNotifyOnDataReceived, monitor, filename.c_str(), cancelFlag, id));
			monitor->Push(CreateInitCurlNotification(url, false));
		}

		CURL* curl = curl_easy_init();

		if (!curl) {
			Logger::Error("DownloadFile: error while initializing curl session to download hash\n");
			result.result = DOWNLOAD_FILE_BAD_CURL;
			return result;
		}

		ScopedCURL scoppedCurl(curl);
		InitCurlSession(curl, parameters, &response);

		if (monitor) {
			monitor->Push(CreateInitCurlNotification(url, true));
			monitor->Push(CreatePerformCurlNotification(url, false));
		}

		result.code = curl_easy_perform(curl);
		if (result.code != CURLE_OK) {
			Logger::Error("DownloadFile: error while downloading file: %s\n", curl_easy_strerror(result.code));
			result.result = DOWNLOAD_FILE_DOWNLOAD_ERROR;
			return result;
		}

		if (monitor) {
			monitor->Push(CreatePerformCurlNotification(url, true));
			monitor->Push(CreateDoneNotification(url));
		}

		result.result = DOWNLOAD_FILE_OK;
		return result;
	}

	void InitCurlSession(CURL* curl, RequestParameters const& request,
		AbstractCurlResponseHandler* handler) {
		struct curl_slist* headers = NULL;

		for (std::string const& header : request.headers) {
			headers = curl_slist_append(headers, header.c_str());
		}

		curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AbstractCurlResponseHandler::ResponseSkeleton);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, handler);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		if (request.maxSpeed) {
			curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, request.maxSpeed);
		}

		if (request.timeout) {
			curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.timeout);
		}

		if (request.serverTimeout) {
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, request.serverTimeout);
		}
	}

	bool MonitorNotifyOnDataReceived(Threading::Monitor<DownloadNotification>* monitor,
		const char* name, std::atomic<bool>* cancel,
		uint32_t id, bool, void*, size_t n, size_t count) {
		DownloadNotification notification;
		bool keepGoing = true;
		if (cancel->load(std::memory_order_acquire)) {
			keepGoing = false;
			notification.type = DOWNLOAD_ABORTED;
			notification.id = id;
		} else {
			notification.name = name;
			notification.type = DOWNLOAD_DATA_RECEIVED;
			notification.data = n * count;
			notification.id = id;
		}
		monitor->Push(notification);
		return keepGoing;
	}

	DownloadNotification CreateNotification(DownloadNotificationType type) {
		DownloadNotification notification;
		notification.type = type;
		return notification;
	}

	DownloadNotification CreateInitCurlNotification(const char* url, bool done) {
		DownloadNotification notification;
		notification.type = done ? DOWNLOAD_INIT_CURL_DONE : DOWNLOAD_INIT_CURL;
		notification.data = url;
		return notification;
	}

	DownloadNotification CreatePerformCurlNotification(const char* url, bool done) {
		DownloadNotification notification;
		notification.type = done ? DOWNLOAD_CURL_PERFORM_DONE : DOWNLOAD_CURL_PERFORM;
		notification.data = url;
		return notification;
	}

	DownloadNotification CreateDoneNotification(const char* url) {
		DownloadNotification notification;
		notification.type = DOWNLOAD_DONE;
		notification.data = url;
		return notification;
	}
}