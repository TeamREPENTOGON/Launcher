#pragma once

#include <string>
#include <utility>

#include <curl/curl.h>

struct CURLRequest {
    std::string url;
    long timeout = 0;
    long serverTimeout = 0;
    curl_off_t maxSpeed = 0;
};