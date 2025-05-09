#include <curl/curl.h>

#include "shared/curl/abstract_response_handler.h"

size_t AbstractCurlResponseHandler::OnData(void* data, size_t size, size_t n) {
	for (OnDataHandlerFn const& fn : _hooks) {
		if (!fn(!_firstReceived, data, size, n)) {
			return CURL_WRITEFUNC_ERROR;
		}
	}

	if (!_firstReceived) {
		_firstReceived = true;
		return OnFirstData(data, size, n);
	} else {
		return OnNewData(data, size, n);
	}
}

size_t AbstractCurlResponseHandler::ResponseSkeleton(void* data, size_t size, size_t n,
	void* userp) {
	AbstractCurlResponseHandler* handler = (AbstractCurlResponseHandler*)userp;
	return handler->OnData(data, size, n);
}

void AbstractCurlResponseHandler::RegisterHook(OnDataHandlerFn fn) {
	_hooks.push_back(fn);
}