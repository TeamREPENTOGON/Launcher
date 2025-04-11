#pragma once

#include <atomic>
#include <future>

#include "shared/github.h"
#include "shared/monitor.h"

class GithubExecutor {
public:
    static GithubExecutor& Instance();

    ~GithubExecutor();

    void Start();
    void Stop();

    std::future<Github::DownloadAsStringResult> AddDownloadAsStringRequest(const char* url,
        std::string& response, Github::DownloadMonitor* monitor);

    std::future<Github::DownloadFileResult> AddDownloadFileRequest(const char* file,
        const char* url, Github::DownloadMonitor* monitor);

    std::future<Github::DownloadAsStringResult> AddFetchReleasesRequest(const char* url,
        rapidjson::Document& response, Github::DownloadMonitor* monitor);

private:
    friend class GithubRequestVisitor;

    struct DownloadAsStringRequest {
        const char* url;
        std::string* response;
        Github::DownloadMonitor* monitor;
        std::promise<Github::DownloadAsStringResult> result;
    };

    struct DownloadFileRequest {
        const char* url;
        const char* file;
        Github::DownloadMonitor* monitor;
        std::promise<Github::DownloadFileResult> result;
    };

    struct FetchReleasesRequest {
        const char* url;
        rapidjson::Document* response;
        Github::DownloadMonitor* monitor;
        std::promise<Github::DownloadAsStringResult> result;
    };

    typedef std::variant<DownloadAsStringRequest, DownloadFileRequest, FetchReleasesRequest> GithubRequest;
    Threading::Monitor<GithubRequest> _requests;

    GithubExecutor();

    void Run();

    std::thread _thread;
    std::atomic<bool> _stop;
};

#define sGithubExecutor (&GithubExecutor::Instance())