#ifndef __APPLE__
#include "platform/app_nap.h"

namespace vivid {

// App Nap is a macOS concept; elsewhere the run loop isn't throttled this way.
void disable_app_nap(const char* /*reason*/) {}

}  // namespace vivid
#endif  // !__APPLE__
