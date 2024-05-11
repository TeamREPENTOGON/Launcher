#pragma once

#include <curl/curl.h>

class ScopedCURL {
public:
	ScopedCURL(CURL* curl);
	~ScopedCURL();

private:
	CURL* _curl;
};