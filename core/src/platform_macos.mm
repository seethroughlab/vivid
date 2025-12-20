// Platform-specific helpers for macOS
// Provides autoreleasepool wrapper for Metal/WebGPU frame loop

#import <Foundation/Foundation.h>
#include "platform_macos.h"

namespace vivid {
namespace platform {

void withAutoreleasePool(const std::function<void()>& fn) {
    @autoreleasepool {
        fn();
    }
}

} // namespace platform
} // namespace vivid
