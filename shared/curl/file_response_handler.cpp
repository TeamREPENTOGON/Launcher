#include "shared/curl/file_response_handler.h"

CurlFileResponse::CurlFileResponse(std::string const& name) {
	_f = fopen(name.c_str(), "wb");
}

CurlFileResponse::~CurlFileResponse() {
	if (_f)
		fclose(_f);
}

size_t CurlFileResponse::OnFirstData(void* data, size_t len, size_t n) {
	return Append(data, len, n);
}

size_t CurlFileResponse::OnNewData(void* data, size_t len, size_t n) {
	return Append(data, len, n);
}

FILE* CurlFileResponse::GetFile() const {
	return _f;
}

size_t CurlFileResponse::Append(void* data, size_t len, size_t n) {
	return fwrite(data, len, n, _f);
}