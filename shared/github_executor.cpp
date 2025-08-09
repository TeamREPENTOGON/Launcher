#include "shared/github_executor.h"
#include "shared/private/curl/curl_request.h"

class GithubRequestVisitor {
public:
    void operator()(GithubExecutor::DownloadStringRequest& r) {
        r.result.set_value(curl::detail::DownloadString(r.request, r.name, r.descriptor));
    }

    void operator()(GithubExecutor::DownloadFileRequest& r) {
        r.result.set_value(curl::detail::DownloadFile(r.request, r.filename, r.descriptor));
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
    bool timedout = false;
    while (!_stop.load(std::memory_order_acquire)) {
        std::optional<GithubRequest> request = _requests.Get(&timedout);
        if (!request) {
            if (!timedout) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        std::visit(GithubRequestVisitor(), *request);
    }
}

void GithubExecutor::Stop() {
    _stop.store(true, std::memory_order_release);
}

std::future<curl::DownloadStringDescriptor> GithubExecutor::AddDownloadStringRequest(
    std::string&& name, curl::RequestParameters const& request,
    std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> const& desc) {
    DownloadStringRequest r;
    r.descriptor = desc;
    r.name = std::move(name);
    r.request = request;

    std::future<curl::DownloadStringDescriptor> result = r.result.get_future();
    _requests.Push(std::move(r));
    return result;
}

std::future<curl::DownloadFileDescriptor> GithubExecutor::AddDownloadFileRequest(
    std::string&& filename, curl::RequestParameters const& request,
    std::shared_ptr<curl::AsynchronousDownloadFileDescriptor> const& desc) {
    DownloadFileRequest r;
    r.descriptor = desc;
    r.filename = std::move(filename);
    r.request = request;

    std::future<curl::DownloadFileDescriptor> result = r.result.get_future();
    _requests.Push(std::move(r));
    return result;
}

/* std::future<Github::DownloadAsStringResult> GithubExecutor::AddFetchReleasesRequest(
    CURLRequest const& request, rapidjson::Document& response, Github::DownloadMonitor* monitor) {
    FetchReleasesRequest r;
    r.request = request;
    r.response = &response;
    r.monitor = monitor;

    std::future<Github::DownloadAsStringResult> result = r.result.get_future();
    _requests.Push(std::move(r));
    return result;
} */