#pragma once

#include <string>

namespace vivid {

struct HttpFetchResult {
    bool ok = false;
    long http_status = 0;
    std::string body;
    std::string error;
};

HttpFetchResult http_get(const std::string& url, long timeout_seconds);

} // namespace vivid
