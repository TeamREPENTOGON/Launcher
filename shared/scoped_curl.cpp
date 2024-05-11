#include "shared/scoped_curl.h"

ScopedCURL::ScopedCURL(CURL* curl) : _curl(curl) {

}

ScopedCURL::~ScopedCURL() {
	if (_curl) {
		curl_easy_cleanup(_curl);
	}
}