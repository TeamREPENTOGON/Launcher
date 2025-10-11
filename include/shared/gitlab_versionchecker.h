#pragma once
#ifndef GITLAB_CHECKER
#define GITLAB_CHECKER = 1


#include <io.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

inline size_t versionwriteresponse(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = (std::string*)userp;
    s->append((char*)contents, total);
    return total;
}

inline bool ShouldDoTimeBasedGitLabSkip() { //only used for the launcher update routine, since ideally we shouldnt really skip this process unless theres a problem....and if theres a problem we would fix it in a launcher update so thats why it skips it once a day just in case, lol
    char* appdataPath = nullptr;
    size_t len = 0;
    _dupenv_s(&appdataPath, &len, "APPDATA");

    std::string basePath;
    if (!appdataPath || !*appdataPath) {
        Logger::Error("AppData not defined?! Using current directory instead.\n");
        basePath = "";
    }
    else {
        basePath = appdataPath;
        free(appdataPath);
    }

    std::string filePath = basePath + "\\REPENTOGONLauncher_Cache\\timestamp.txt";

    std::ifstream prev(filePath);
    std::time_t lastTime = 0;
    if (prev.good())
        prev >> lastTime;
    prev.close();

    std::time_t now = std::time(nullptr);
    bool ret = (lastTime == 0) || (difftime(now, lastTime) >= 24 * 60 * 60); //more than a day

    if (ret) {
        size_t pos = filePath.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string dir = filePath.substr(0, pos);
            CreateDirectoryA(dir.c_str(), nullptr);
        }
        std::ofstream out(filePath, std::ios::trunc);
        out << now;
        out.close();
    }

    return ret;
}

inline bool RemoteGitLabVersionMatches(std::string versionfilename, std::string currversion) {
    const std::string url =
        "https://gitlab.com/repentogon/versiontracker/-/raw/main/" + versionfilename + ".txt";

    CURL* curl = curl_easy_init();
    if (!curl)
        return false; //dont care, we have github as backup, let it error there, lol

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: rgon-version-getter/6.9");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, versionwriteresponse);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {        
        return false; //dont care, we have github as backup, let it error there, lol
    }

    if (http_code < 200 || http_code >= 300)
    {
        return false; //dont care, we have github as backup, let it error there, lol
    }

    return response == currversion;
}

#endif 