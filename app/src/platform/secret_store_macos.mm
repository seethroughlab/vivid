#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include "platform/secret_store.h"

// Keychain-backed secret store for the Gemini API key (ADR-0026). A generic-password item keyed by
// (service, account). Foundation-only, NO ARC (manual retain/release + plain toll-free-bridged casts,
// matching macos_app_nap.mm). Follows the platform-seam convention.
namespace vivid {

// Returns an autoreleased mutable query dict with class/service/account set.
static NSMutableDictionary* base_query(const std::string& service, const std::string& account) {
    NSDictionary* d = @{
        (id)kSecClass:       (id)kSecClassGenericPassword,
        (id)kSecAttrService: [NSString stringWithUTF8String:service.c_str()],
        (id)kSecAttrAccount: [NSString stringWithUTF8String:account.c_str()],
    };
    return [[d mutableCopy] autorelease];
}

bool secret_set(const std::string& service, const std::string& account, const std::string& value) {
    @autoreleasepool {
        NSMutableDictionary* q = base_query(service, account);
        SecItemDelete((CFDictionaryRef)q);   // replace: delete any existing, then add
        q[(id)kSecValueData] = [NSData dataWithBytes:value.data() length:value.size()];
        q[(id)kSecAttrAccessible] = (id)kSecAttrAccessibleWhenUnlocked;
        return SecItemAdd((CFDictionaryRef)q, NULL) == errSecSuccess;
    }
}

bool secret_get(const std::string& service, const std::string& account, std::string& out) {
    @autoreleasepool {
        NSMutableDictionary* q = base_query(service, account);
        q[(id)kSecReturnData] = @YES;
        q[(id)kSecMatchLimit] = (id)kSecMatchLimitOne;
        CFTypeRef res = NULL;
        if (SecItemCopyMatching((CFDictionaryRef)q, &res) != errSecSuccess || !res) return false;
        NSData* data = (NSData*)res;   // +1 from Security; released below
        out.assign(static_cast<const char*>(data.bytes), data.length);
        CFRelease(res);
        return true;
    }
}

bool secret_delete(const std::string& service, const std::string& account) {
    @autoreleasepool {
        OSStatus st = SecItemDelete((CFDictionaryRef)base_query(service, account));
        return st == errSecSuccess || st == errSecItemNotFound;
    }
}

}  // namespace vivid

#endif  // __APPLE__
