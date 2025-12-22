// Platform stubs for non-macOS platforms

#include <vivid/platform_macos.h>

namespace vivid {
namespace platform {

// No-op on non-macOS platforms - just execute the function directly
void withAutoreleasePool(const std::function<void()>& fn) {
    fn();
}

} // namespace platform
} // namespace vivid
