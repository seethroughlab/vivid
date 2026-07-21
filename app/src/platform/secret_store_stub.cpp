#ifndef __APPLE__
#include "platform/secret_store.h"

namespace vivid {

// No system secret store wired off macOS yet. Returns false so the caller treats the key as unset
// (music-eval reports "unconfigured" and fails closed).
bool secret_set(const std::string& /*service*/, const std::string& /*account*/, const std::string& /*value*/) { return false; }
bool secret_get(const std::string& /*service*/, const std::string& /*account*/, std::string& /*out*/) { return false; }
bool secret_delete(const std::string& /*service*/, const std::string& /*account*/) { return false; }

}  // namespace vivid
#endif  // !__APPLE__
