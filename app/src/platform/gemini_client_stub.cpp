#ifndef __APPLE__
#include "platform/gemini_client.h"

namespace vivid {

// No system HTTPS client off macOS yet (the app links no OpenSSL/curl). The music-eval feature is
// macOS-only for now; the caller sees a transport failure and reports it as unavailable.
void gemini_post_json(const std::string& /*url*/, const std::string& /*body*/, double /*timeout_s*/,
                      std::function<void(bool, int, std::string)> cb) {
    cb(false, 0, "gemini_client: unsupported on this platform");
}

}  // namespace vivid
#endif  // !__APPLE__
