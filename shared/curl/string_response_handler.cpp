#include "shared/curl/string_response_handler.h"

size_t CurlStringResponse::OnFirstData(void* data, size_t len, size_t n) {
	return Append(data, len, n);
}

size_t CurlStringResponse::OnNewData(void* data, size_t len, size_t n) {
	return Append(data, len, n);
}

std::string const& CurlStringResponse::GetData() const {
	return _data;
}

size_t CurlStringResponse::Append(void* data, size_t len, size_t n) {
	_data.append((char*)data, len * n);
	return len * n;
}