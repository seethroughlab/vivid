#pragma once

#include <functional>
#include <string>

namespace vivid {

/// Fire-and-forget HTTPS POST of a JSON body, async. The reply (or a transport error) is delivered
/// to `cb` ON A BACKGROUND THREAD when the request finishes — callers must make `cb` thread-safe.
///   ok    = the request completed and got an HTTP response (`http_status` may still be 4xx/5xx).
///   !ok   = a transport failure (no network, bad URL, timeout); `body` carries the error text.
/// macOS routes this through NSURLSession (system trust store, HTTPS); other platforms are a noop
/// that calls back with ok=false. Used for the in-app Gemini music-eval call (ADR-0026).
void gemini_post_json(const std::string& url, const std::string& body, double timeout_s,
                      std::function<void(bool ok, int http_status, std::string body)> cb);

}  // namespace vivid
