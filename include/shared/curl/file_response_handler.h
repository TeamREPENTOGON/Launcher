#pragma once

#include <string>

#include "shared/curl/abstract_response_handler.h"

class CurlFileResponse : public AbstractCurlResponseHandler {
public:
	CurlFileResponse(std::string const& name);
	~CurlFileResponse();

	size_t OnFirstData(void* data, size_t len, size_t n);
	size_t OnNewData(void* data, size_t len, size_t n);
	FILE* GetFile() const;

private:
	size_t Append(void* data, size_t len, size_t n);

	FILE* _f = NULL;
};