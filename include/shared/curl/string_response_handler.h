#pragma once

#include <string>

#include "shared/curl/abstract_response_handler.h"

class CurlStringResponse : public AbstractCurlResponseHandler {
public:
	size_t OnFirstData(void* data, size_t len, size_t n);
	size_t OnNewData(void* data, size_t len, size_t n);
	std::string const& GetData() const;

private:
	size_t Append(void* data, size_t len, size_t n);

	std::string _data;
};