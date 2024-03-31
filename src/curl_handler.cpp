#include "launcher/curl_handler.h"

size_t AbstractCurlResponseHandler::OnData(void* data, size_t size, size_t n) {
	if (!_firstReceived) {
		_firstReceived = true;
		return OnFirstData(data, size, n);
	}
	else {
		return OnNewData(data, size, n);
	}
}

size_t AbstractCurlResponseHandler::ResponseSkeleton(void* data, size_t size, size_t n,
	void* userp) {
	AbstractCurlResponseHandler* handler = (AbstractCurlResponseHandler*)userp;
	return handler->OnData(data, size, n);
}