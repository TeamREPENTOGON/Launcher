#pragma once

#include <atomic>
#include <future>

#include "shared/curl_request.h"
#include "shared/monitor.h"

class GithubExecutor {
public:
    static GithubExecutor& Instance();

    ~GithubExecutor();

    void Start();
    void Stop();

    std::future<curl::DownloadStringDescriptor> AddDownloadStringRequest(std::string&& name,
        curl::RequestParameters const& request,
        std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> const& desc);

    std::future<curl::DownloadFileDescriptor> AddDownloadFileRequest(std::string&& filename,
        curl::RequestParameters const& request,
        std::shared_ptr<curl::AsynchronousDownloadFileDescriptor> const& desc);

private:
    friend class GithubRequestVisitor;

    struct DownloadStringRequest {
        curl::RequestParameters                                     request;
        std::string                                                 name;
        std::shared_ptr<curl::AsynchronousDownloadStringDescriptor> descriptor;
        std::promise<curl::DownloadStringDescriptor>                result;
    };

    struct DownloadFileRequest {
        curl::RequestParameters                                     request;
        std::string                                                 filename;
        std::shared_ptr<curl::AsynchronousDownloadFileDescriptor>   descriptor;
        std::promise<curl::DownloadFileDescriptor>                  result;
    };

    typedef std::variant<DownloadStringRequest, DownloadFileRequest> GithubRequest;
    Threading::Monitor<GithubRequest> _requests;

    GithubExecutor();

    void Run();

    std::thread _thread;
    std::atomic<bool> _stop;
};

#define sGithubExecutor (&GithubExecutor::Instance())