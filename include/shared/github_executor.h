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

    std::future<Github::DownloadAsStringResult> AddDownloadAsStringRequest(CURLRequest const& request,
        std::string&& name, std::string& response, Github::DownloadMonitor* monitor);

    std::future<Github::DownloadFileResult> AddDownloadFileRequest(const char* file,
        CURLRequest const& request, Github::DownloadMonitor* monitor);

    std::future<Github::DownloadAsStringResult> AddFetchReleasesRequest(CURLRequest const& request,
        rapidjson::Document& response, Github::DownloadMonitor* monitor);

private:
    friend class GithubRequestVisitor;

    struct DownloadAsStringRequest {
        CURLRequest request;
        std::string name;
        std::string* response;
        Github::DownloadMonitor* monitor;
        std::promise<Github::DownloadAsStringResult> result;
    };

    struct DownloadFileRequest {
        CURLRequest request;
        const char* file;
        Github::DownloadMonitor* monitor;
        std::promise<Github::DownloadFileResult> result;
    };

    struct FetchReleasesRequest {
        CURLRequest request;
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