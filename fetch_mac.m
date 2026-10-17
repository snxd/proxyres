#import <Foundation/Foundation.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include "fetch.h"
#include "log.h"
#include "util.h"

#define FETCH_REQUEST_TIMEOUT_SEC  8
#define FETCH_RESOURCE_TIMEOUT_SEC 12

// Accumulates the response body and enforces SCRIPT_MAX while the body is received
@interface ProxyresFetchDelegate : NSObject <NSURLSessionDataDelegate> {
   @public
    // Points at the caller's url, which stays valid because fetch_get waits for the task
    const char *url;
    NSMutableData *body;
    int32_t error;
    dispatch_semaphore_t done;
}
@end

@implementation ProxyresFetchDelegate

// Follow only http and https redirects, and never downgrade from https to http
- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completion_handler {
    UNUSED(session);
    NSString *from_scheme = response.URL.scheme.lowercaseString;
    NSString *to_scheme = request.URL.scheme.lowercaseString;
    bool is_http = [to_scheme isEqualToString:@"http"];
    bool is_https = [to_scheme isEqualToString:@"https"];
    if ((!is_http && !is_https) || (is_http && [from_scheme isEqualToString:@"https"])) {
        error = EIO;
        log_error("Unsafe redirect from %s to %s", response.URL.absoluteString.UTF8String,
                  request.URL.absoluteString.UTF8String);
        [task cancel];
        completion_handler(nil);
        return;
    }
    completion_handler(request);
}

- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completion_handler {
    UNUSED(session);
    UNUSED(task);
    if (error) {
        completion_handler(NSURLSessionResponseCancel);
        return;
    }
    if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
        NSInteger status_code = ((NSHTTPURLResponse *)response).statusCode;
        if (status_code / 100 != 2) {
            error = EIO;
            log_error("Unexpected HTTP status %ld", (long)status_code);
            completion_handler(NSURLSessionResponseCancel);
            return;
        }
    }
    if (response.expectedContentLength >= SCRIPT_MAX) {
        error = EIO;
        log_error("Script size exceeds maximum (%" PRId64 ")", (int64_t)response.expectedContentLength);
        completion_handler(NSURLSessionResponseCancel);
        return;
    }
    completion_handler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data {
    UNUSED(session);
    if (error)
        return;
    if (body.length + data.length >= SCRIPT_MAX) {
        error = EIO;
        log_error("Script size exceeds maximum (%" PRId64 ")", (int64_t)(body.length + data.length));
        [task cancel];
        return;
    }
    [body appendData:data];
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)task_error {
    UNUSED(session);
    UNUSED(task);
    if (task_error && !error) {
        error = EIO;
        log_error("Unable to fetch %s (%s)", url, task_error.localizedDescription.UTF8String);
    }
    dispatch_semaphore_signal(done);
}

@end

// Fetch proxy auto configuration using NSURLSession, which supports TLS
char *fetch_get(const char *url, int32_t *error) {
    if (!url) {
        if (error)
            *error = EINVAL;
        return NULL;
    }

    @autoreleasepool {
        NSString *url_string = [NSString stringWithUTF8String:url];
        NSURL *ns_url = url_string ? [NSURL URLWithString:url_string] : nil;

        // Restrict to http and https. The PAC url can come from untrusted DHCP option 252, and
        // NSURLSession would otherwise also load file and data urls. A nil url is rejected too.
        NSString *scheme = ns_url.scheme.lowercaseString;
        if (![scheme isEqualToString:@"http"] && ![scheme isEqualToString:@"https"]) {
            log_error("Unsupported proxy auto config url %s (%" PRId32 ")", url, (int32_t)EINVAL);
            if (error)
                *error = EINVAL;
            return NULL;
        }

        NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForRequest = FETCH_REQUEST_TIMEOUT_SEC;
        config.timeoutIntervalForResource = FETCH_RESOURCE_TIMEOUT_SEC;
        // Fetch the PAC directly, because the proxy may depend on the PAC itself
        config.connectionProxyDictionary = @{};

        ProxyresFetchDelegate *delegate = [[ProxyresFetchDelegate alloc] init];
        delegate->url = url;
        delegate->body = [NSMutableData data];
        delegate->done = dispatch_semaphore_create(0);

        NSURLSession *session = [NSURLSession sessionWithConfiguration:config delegate:delegate delegateQueue:nil];

        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:ns_url];
        [request setValue:@"application/x-ns-proxy-autoconfig" forHTTPHeaderField:@"Accept"];

        NSURLSessionDataTask *task = [session dataTaskWithRequest:request];
        if (!task) {
            log_error("Unable to create task to fetch %s (%" PRId32 ")", url, (int32_t)EIO);
            [session invalidateAndCancel];
            if (error)
                *error = EIO;
            return NULL;
        }
        [task resume];
        dispatch_semaphore_wait(delegate->done, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];

        int32_t err = delegate->error;
        if (!err && delegate->body.length == 0) {
            err = EIO;
            log_error("Empty proxy auto config script (%" PRId32 ")", err);
        }

        char *script = NULL;
        if (!err) {
            NSUInteger length = delegate->body.length;
            script = (char *)malloc(length + 1);
            if (script) {
                memcpy(script, delegate->body.bytes, length);
                script[length] = 0;
            } else {
                err = ENOMEM;
                log_error("Unable to allocate memory for %s (%" PRId32 ")", "script", err);
            }
        }

        if (error)
            *error = err;
        return script;
    }
}

bool fetch_global_init(void) {
    return true;
}

bool fetch_global_cleanup(void) {
    return true;
}
