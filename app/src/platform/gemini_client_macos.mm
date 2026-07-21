#ifdef __APPLE__
#import <Foundation/Foundation.h>

#include "platform/gemini_client.h"

#include <utility>

// The in-app HTTPS client for the Gemini music-eval call (ADR-0026). NSURLSession gives us TLS via
// the system trust store with zero new dependencies — cpp-httplib is compiled without OpenSSL and is
// server-only. Foundation-only, no ARC (this target manages retain/release manually), matching the
// platform-seam convention (see macos_app_nap.mm). The completion handler runs on NSURLSession's
// background queue, so the callback must be thread-safe.
namespace vivid {

void gemini_post_json(const std::string& url, const std::string& body, double timeout_s,
                      std::function<void(bool, int, std::string)> cb) {
    @autoreleasepool {
        NSURL* nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!nsurl) { cb(false, 0, "gemini_client: bad url"); return; }

        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsurl];
        req.HTTPMethod = @"POST";
        [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
        req.timeoutInterval = (timeout_s > 0.0) ? timeout_s : 60.0;

        // The completion block runs after this function (and its autorelease pool) returns, so the
        // std::function can't live on the stack — hold it on the heap and delete it in the block.
        auto* holder = new std::function<void(bool, int, std::string)>(std::move(cb));

        NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:req
            completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
                std::function<void(bool, int, std::string)> fn = *holder;
                delete holder;
                if (err) {
                    const char* msg = [[err localizedDescription] UTF8String];
                    fn(false, 0, std::string(msg ? msg : "gemini_client: network error"));
                    return;
                }
                int status = 0;
                if ([resp isKindOfClass:[NSHTTPURLResponse class]])
                    status = static_cast<int>([(NSHTTPURLResponse*)resp statusCode]);
                std::string out;
                if (data && data.length) out.assign(static_cast<const char*>(data.bytes), data.length);
                fn(true, status, std::move(out));
            }];
        [task resume];   // a resumed task is retained by the session until it completes
    }
}

}  // namespace vivid

#endif  // __APPLE__
