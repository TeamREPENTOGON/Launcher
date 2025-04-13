#include "shared/github_executor.h"

class GithubRequestVisitor {
public:
    void operator()(GithubExecutor::DownloadAsStringRequest& r) {
        r.result.set_value(Github::DownloadAsString(r.request, r.name.c_str(),
            *r.response, r.monitor));
    }

    void operator()(GithubExecutor::DownloadFileRequest& r) {
        r.result.set_value(Github::DownloadFile(r.file, r.request, r.monitor));
    }

    void operator()(GithubExecutor::FetchReleasesRequest& r) {
        r.result.set_value(Github::FetchReleaseInfo(r.request, *r.response, r.monitor));
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

std::future<Github::DownloadAsStringResult> GithubExecutor::AddDownloadAsStringRequest(
    CURLRequest const& request, std::string&& name, std::string& response,
    Github::DownloadMonitor* monitor) {
    DownloadAsStringRequest r;
    r.request = request;
    r.name = std::move(name);
    r.response = &response;
    r.monitor = monitor;

    std::future<Github::DownloadAsStringResult> result = r.result.get_future();
    _requests.Push(std::move(r));
    return result;
}

std::future<Github::DownloadFileResult> GithubExecutor::AddDownloadFileRequest(const char* file,
    CURLRequest const& request, Github::DownloadMonitor* monitor) {
    DownloadFileRequest r;
    r.request = request;
    r.file = file;
    r.monitor = monitor;

    std::future<Github::DownloadFileResult> result = r.result.get_future();
    _requests.Push(std::move(r));
    return result;
}

std::future<Github::DownloadAsStringResult> GithubExecutor::AddFetchReleasesRequest(
    CURLRequest const& request, rapidjson::Document& response, Github::DownloadMonitor* monitor) {
    FetchReleasesRequest r;
    r.request = request;
    r.response = &response;
    r.monitor = monitor;

    std::future<Github::DownloadAsStringResult> result = r.result.get_future();
    _requests.Push(std::move(r));
    return result;
}