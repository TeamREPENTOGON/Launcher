#include "shared/github_executor.h"

class GithubRequestVisitor {
public:
    void operator()(GithubExecutor::DownloadAsStringRequest& request) {
        request.result.set_value(Github::DownloadAsString(request.url, *request.response, request.monitor));
    }

    void operator()(GithubExecutor::DownloadFileRequest& request) {
        request.result.set_value(Github::DownloadFile(request.file, request.url, request.monitor));
    }

    void operator()(GithubExecutor::FetchReleasesRequest& request) {
        request.result.set_value(Github::FetchReleaseInfo(request.url, *request.response, request.monitor));
    }
};

GithubExecutor& GithubExecutor::Instance() {
    static GithubExecutor executor;
    return executor;
}

GithubExecutor::~GithubExecutor() {
    Stop();
    _thread.join();
}

GithubExecutor::GithubExecutor() {
    _stop.store(false, std::memory_order_release);
}

void GithubExecutor::Start() {
    _thread = std::thread(&GithubExecutor::Run, this);
}

void GithubExecutor::Run() {
    while (!_stop.load(std::memory_order_acquire)) {
        std::optional<GithubRequest> request = _requests.Get();
        if (!request)
            continue;

        std::visit(GithubRequestVisitor(), *request);
    }
}

void GithubExecutor::Stop() {
    _stop.store(true, std::memory_order_release);
}

std::future<Github::DownloadAsStringResult> GithubExecutor::AddDownloadAsStringRequest(const char* url,
    std::string& response, Github::DownloadMonitor* monitor) {
    DownloadAsStringRequest request;
    request.url = url;
    request.response = &response;
    request.monitor = monitor;

    std::future<Github::DownloadAsStringResult> result = request.result.get_future();
    _requests.Push(std::move(request));
    return result;
}

std::future<Github::DownloadFileResult> GithubExecutor::AddDownloadFileRequest(const char* file,
    const char* url, Github::DownloadMonitor* monitor) {
    DownloadFileRequest request;
    request.file = file;
    request.url = url;
    request.monitor = monitor;

    std::future<Github::DownloadFileResult> result = request.result.get_future();
    _requests.Push(std::move(request));
    return result;
}

std::future<Github::DownloadAsStringResult> GithubExecutor::AddFetchReleasesRequest(const char* url,
    rapidjson::Document& response, Github::DownloadMonitor* monitor) {
    FetchReleasesRequest request;
    request.url = url;
    request.response = &response;
    request.monitor = monitor;

    std::future<Github::DownloadAsStringResult> result = request.result.get_future();
    _requests.Push(std::move(request));
    return result;
}