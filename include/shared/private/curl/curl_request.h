#pragma once

#include "shared/curl_request.h"

namespace curl::detail {
    DownloadStringDescriptor DownloadString(RequestParameters const& parameters,
        std::string const& name,
        std::shared_ptr<AsynchronousDownloadStringDescriptor> const& desc);

    DownloadFileDescriptor DownloadFile(RequestParameters const& parameters,
        std::string const& filename,
        std::shared_ptr<AsynchronousDownloadFileDescriptor> const& desc);

    std::optional<std::string> GetCurlProxyString();
}