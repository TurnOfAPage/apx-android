// update_check_mac.mm —— macOS 的 HTTPS GET 实现（NSURLSession）。
// update_check.cpp 按平台分派到这里：先直连 api.github.com，失败自动回退 gh-proxy
// 镜像（回退策略在 update_check.cpp 主流程，与 Windows 完全同一条代码路径）。
//
// 编译要求：ARC（CMake 对 apxpc_service 的 OBJCXX 加 -fobjc-arc）—— completionHandler
// block 跨异步生命周期持有对象，MRC 下 semaphore 唤醒后对象可能已被释放。
#include "apxpc/app/update_check.hpp"

#if defined(__APPLE__) && !defined(_WIN32)

#import <Foundation/Foundation.h>

#include <string>

namespace apxpc::app {

// 与 update_check.cpp 中的声明严格一致（签名不匹配会在链接期报 undefined，能掲住）。
// 返回 HTTP 状态码；0 = 连接层失败（err 写原因）。NSURLSession 默认跟随 302 重定向，
// gh-proxy 镜像链路因此自动工作。
unsigned httpsGetMac(const std::string& host, const std::string& path,
                     std::string& body, std::string& err) {
    @autoreleasepool {
        if (host.empty() || path.empty()) { err = "URL 为空"; return 0; }
        NSString* urlStr = [NSString stringWithFormat:@"https://%s%s",
                                                      host.c_str(), path.c_str()];
        NSURL* url = [NSURL URLWithString:urlStr];
        if (!url) { err = "URL 非法"; return 0; }

        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url];
        req.HTTPMethod = @"GET";
        req.timeoutInterval = 8.0;   // 对齐 WinHTTP（5s 连接 / 8s 读）
        req.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block unsigned status = 0;
        __block bool finished = false;
        __block NSString* errDesc = nil;
        __block NSData* data = nil;

        NSURLSessionDataTask* task =
            [[NSURLSession sharedSession] dataTaskWithRequest:req
                completionHandler:^(NSData* d, NSURLResponse* r, NSError* e) {
                    if (e) {
                        errDesc = [e localizedDescription];
                    } else {
                        if ([r isKindOfClass:[NSHTTPURLResponse class]])
                            status = (unsigned)[(NSHTTPURLResponse*)r statusCode];
                        data = d;
                    }
                    finished = true;
                    dispatch_semaphore_signal(sem);
                }];
        [task resume];

        // 兕底等待：timeoutInterval 8s + 余量；超时取消并按连接层失败上报
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW,
                                                   (int64_t)(15 * NSEC_PER_SEC)));
        if (!finished) {
            [task cancel];
            err = "HTTP 超时（15s）";
            return 0;
        }
        if (errDesc) {
            const char* u = [errDesc UTF8String];
            err = u ? u : "请求失败";
            return 0;
        }
        if (data) body.assign(static_cast<const char*>(data.bytes), data.length);
        return status;
    }
}

}  // namespace apxpc::app

#endif  // __APPLE__
