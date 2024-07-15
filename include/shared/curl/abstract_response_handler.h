#pragma once

#include <functional>
#include <vector>

class AbstractCurlResponseHandler {
	typedef std::function<void(bool, void*, size_t, size_t)> OnDataHandlerFn;

public:
	virtual size_t OnFirstData(void* data, size_t size, size_t n) { return size * n; }
	virtual size_t OnNewData(void* data, size_t size, size_t n) { return size * n;  }

	static size_t ResponseSkeleton(void* data, size_t size, size_t n, void* userp);

	void RegisterHook(OnDataHandlerFn fn);

private:
	size_t OnData(void* data, size_t size, size_t n);
	bool _firstReceived = false;

	std::vector<OnDataHandlerFn> _hooks;
};