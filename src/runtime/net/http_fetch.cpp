#include "runtime/net/http_fetch.h"

#include <curl/curl.h>

namespace vivid {

static constexpr size_t kMaxResponseBytes = 1 * 1024 * 1024; // 1 MB

struct WriteContext {
    std::string* body;
    bool limit_exceeded = false;
};

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteContext*>(userdata);
    size_t bytes = size * nmemb;
    if (ctx->body->size() + bytes > kMaxResponseBytes) {
        ctx->limit_exceeded = true;
        return 0; // abort transfer
    }
    ctx->body->append(ptr, bytes);
    return bytes;
}

HttpFetchResult http_get(const std::string& url, long timeout_seconds) {
    HttpFetchResult result;

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "failed to initialize curl";
        return result;
    }

    WriteContext ctx{&result.body};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        if (ctx.limit_exceeded) {
            result.error = "response exceeded 1 MB limit";
        } else {
            result.error = curl_easy_strerror(res);
        }
        curl_easy_cleanup(curl);
        return result;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_status);
    if (result.http_status != 0) {
        if (result.http_status < 200 || result.http_status >= 300) {
            result.error = "HTTP " + std::to_string(result.http_status);
            curl_easy_cleanup(curl);
            return result;
        }
    }

    result.ok = true;
    curl_easy_cleanup(curl);
    return result;
}

} // namespace vivid
