#pragma once

class AbstractCurlResponseHandler {
public:
	virtual size_t OnFirstData(void* data, size_t size, size_t n) { return size * n; }
	virtual size_t OnNewData(void* data, size_t size, size_t n) { return size * n;  }

	static size_t ResponseSkeleton(void* data, size_t size, size_t n, void* userp);

private:
	size_t OnData(void* data, size_t size, size_t n);
	bool _firstReceived = false;
};