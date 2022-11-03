#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dlfcn.h>

#include <JavaScriptCore/JavaScript.h>

#include "execute.h"
#include "execute_jscore.h"
#include "log.h"
#include "mozilla_js.h"
#include "util.h"

typedef struct g_proxy_execute_jscore_s {
    // JSCoreGTK module
    void *module;
    // Class functions
    JSClassRef (*JSClassCreate)(const JSClassDefinition *class_def);
    // Object functions
    JSObjectRef (*JSObjectMake)(JSContextRef ctx, JSClassRef cls, void *Data);
    JSObjectRef (*JSObjectMakeFunctionWithCallback)(JSContextRef ctx, JSStringRef name,
                                                    JSObjectCallAsFunctionCallback callback);
    void *(*JSObjectGetPrivate)(JSObjectRef object);
    bool (*JSObjectSetPrivate)(JSObjectRef object, void *data);
    void (*JSObjectSetProperty)(JSContextRef context, JSObjectRef object, JSStringRef name, JSValueRef value,
                                JSPropertyAttributes attribs, JSValueRef *exception);
    JSValueRef (*JSObjectGetProperty)(JSContextRef ctx, JSObjectRef object, JSStringRef name, JSValueRef *exception);
    // Context functions
    JSObjectRef (*JSContextGetGlobalObject)(JSContextRef ctx);
    // Value functions
    bool (*JSValueIsString)(JSContextRef ctx, JSValueRef value);
    double (*JSValueIsNumber)(JSContextRef ctx, JSValueRef value);
    JSObjectRef (*JSValueToObject)(JSContextRef ctx, JSValueRef value, JSValueRef *exception);
    JSStringRef (*JSValueToStringCopy)(JSContextRef ctx, JSValueRef value, JSValueRef *exception);
    double (*JSValueToNumber)(JSContextRef ctx, JSValueRef value, JSValueRef *exception);
    JSValueRef (*JSValueMakeNull)(JSContextRef ctx);
    JSValueRef (*JSValueMakeBoolean)(JSContextRef ctx, bool value);
    JSValueRef (*JSValueMakeString)(JSContextRef ctx, JSStringRef str);
    // String functions
    JSStringRef (*JSStringCreateWithUTF8CString)(const char *str);
    size_t (*JSStringGetUTF8CString)(JSStringRef str, char *buffer, size_t buffer_size);
    size_t (*JSStringGetMaximumUTF8CStringSize)(JSStringRef str);
    void (*JSStringRelease)(JSStringRef str);
    // Global context functions
    JSGlobalContextRef (*JSGlobalContextCreate)(JSClassRef global_object_cls);
    void (*JSGlobalContextRelease)(JSGlobalContextRef global_ctx);
    // Exception functions
    JSValueRef (*JSEvaluateScript)(JSContextRef ctx, JSStringRef script, JSObjectRef object, JSStringRef source_url,
                                   int start_line_num, JSValueRef *exception);
    // Garbage collection functions
    void (*JSGarbageCollect)(JSContextRef ctx);
} g_proxy_execute_jscore_s;

g_proxy_execute_jscore_s g_proxy_execute_jscore;

typedef struct proxy_execute_jscore_s {
    // Execute error
    int32_t error;
    // Proxy list
    char *list;
} proxy_execute_jscore_s;

static char *js_string_dup_to_utf8(JSStringRef str) {
    int32_t utf8_string_size = 0;
    char *utf8_string = NULL;

    if (!str)
        return NULL;

    utf8_string_size = g_proxy_execute_jscore.JSStringGetMaximumUTF8CStringSize(str) + 1;
    utf8_string = (char *)calloc(utf8_string_size, sizeof(char));
    if (!utf8_string)
        return NULL;
    g_proxy_execute_jscore.JSStringGetUTF8CString(str, utf8_string, utf8_string_size);
    return utf8_string;
}

double js_object_get_double_property(JSContextRef ctx, JSObjectRef object, char *name, JSValueRef *exception) {
    JSValueRef property = NULL;

    JSStringRef name_string = g_proxy_execute_jscore.JSStringCreateWithUTF8CString(name);
    if (name_string) {
        property = g_proxy_execute_jscore.JSObjectGetProperty(ctx, object, name_string, exception);
        g_proxy_execute_jscore.JSStringRelease(name_string);
    }

    if (property && g_proxy_execute_jscore.JSValueIsNumber(ctx, property))
        return g_proxy_execute_jscore.JSValueToNumber(ctx, property, exception);
    return 0;
}

JSStringRef js_object_get_string_property(JSContextRef ctx, JSObjectRef object, char *name, JSValueRef *exception) {
    JSValueRef property = NULL;

    JSStringRef name_string = g_proxy_execute_jscore.JSStringCreateWithUTF8CString(name);
    if (name_string) {
        property = g_proxy_execute_jscore.JSObjectGetProperty(ctx, object, name_string, exception);
        g_proxy_execute_jscore.JSStringRelease(name_string);
    }

    if (property && g_proxy_execute_jscore.JSValueIsString(ctx, property))
        return g_proxy_execute_jscore.JSValueToStringCopy(ctx, property, exception);

    return NULL;
}

static void js_print_exception(JSContextRef ctx, JSValueRef exception) {
    JSStringRef MessageString = NULL;
    bool printed = false;
    char *Message = NULL;
    int Line = 0;

    if (!exception)
        return;

    JSObjectRef exception_object = g_proxy_execute_jscore.JSValueToObject(ctx, exception, NULL);
    if (exception_object) {
        int line = js_object_get_double_property(ctx, exception_object, "line", NULL);
        JSStringRef message_string = js_object_get_string_property(ctx, exception_object, "message", NULL);
        if (message_string) {
            char *message = js_string_dup_to_utf8(message_string);
            if (message) {
                printf("EXCEPTION: %s (line %d)\n", message, line);
                free(message);
                printed = true;
            }
            g_proxy_execute_jscore.JSStringRelease(message_string);
        }
    }

    if (!printed) {
        LOG_ERROR("Unable to print unknown exception object\n");
        return;
    }
}

static JSValueRef proxy_execute_jscore_dns_resolve(JSContextRef ctx, JSObjectRef function, JSObjectRef object,
                                                      size_t argc, const JSValueRef argv[], JSValueRef *exception) {
    if (argc != 1)
        return NULL;
    if (!g_proxy_execute_jscore.JSValueIsString(ctx, argv[0]))
        return NULL;

    JSStringRef host_string = g_proxy_execute_jscore.JSValueToStringCopy(ctx, argv[0], NULL);
    if (host_string == NULL)
        return NULL;

    char *host = js_string_dup_to_utf8(host_string);
    g_proxy_execute_jscore.JSStringRelease(host_string);
    if (host == NULL)
        return NULL;

    char *address = dns_resolve(host, NULL);
    free(host);
    if (!address)
        return NULL;

    JSStringRef address_string = g_proxy_execute_jscore.JSStringCreateWithUTF8CString(address);
    free(address);
    if (!address_string)
        return NULL;

    JSValueRef address_value = g_proxy_execute_jscore.JSValueMakeString(ctx, address_string);
    g_proxy_execute_jscore.JSStringRelease(address_string);
    return address_value;
}

static JSValueRef proxy_execute_jscore_my_ip_address(JSContextRef ctx, JSObjectRef function, JSObjectRef object,
                                                        size_t argc, const JSValueRef argv[], JSValueRef *exception) {
    char *address = dns_resolve(NULL, NULL);
    if (!address)
        return NULL;

    JSStringRef host_string = g_proxy_execute_jscore.JSStringCreateWithUTF8CString(address);
    free(address);

    if (!host_string)
        return NULL;

    JSValueRef host_value = g_proxy_execute_jscore.JSValueMakeString(ctx, host_string);
    g_proxy_execute_jscore.JSStringRelease(host_string);
    if (!host_value)
        return NULL;

    return proxy_execute_jscore_dns_resolve(ctx, function, object, 1, &host_value, NULL);
}

bool proxy_execute_jscore_get_proxies_for_url(void *ctx, const char *script, const char *url) {
    proxy_execute_jscore_s *proxy_execute = (proxy_execute_jscore_s *)ctx;
    JSObjectRef function = NULL;
    JSStringRef function_name = NULL;
    JSGlobalContextRef global = NULL;
    JSValueRef exception = NULL;
    char find_proxy[4096];
    bool success = false;

    if (!proxy_execute)
        goto jscoregtk_execute_error;

    global = g_proxy_execute_jscore.JSGlobalContextCreate(NULL);
    if (!global)
        goto jscoregtk_execute_error;

    // Register dnsResolve C function
    function_name = g_proxy_execute_jscore.JSStringCreateWithUTF8CString("dnsResolve");
    if (!function_name)
        goto jscoregtk_execute_error;
    function = g_proxy_execute_jscore.JSObjectMakeFunctionWithCallback(global, function_name,
                                                                          proxy_execute_jscore_dns_resolve);
    if (!function)
        goto jscoregtk_execute_error;

    g_proxy_execute_jscore.JSObjectSetProperty(global, g_proxy_execute_jscore.JSContextGetGlobalObject(global),
                                                  function_name, function, kJSPropertyAttributeNone, NULL);
    g_proxy_execute_jscore.JSStringRelease(function_name);
    function_name = NULL;

    // Register myIpAddress C function
    function_name = g_proxy_execute_jscore.JSStringCreateWithUTF8CString("myIpAddress");
    if (!function_name)
        goto jscoregtk_execute_error;
    function = g_proxy_execute_jscore.JSObjectMakeFunctionWithCallback(global, function_name,
                                                                          proxy_execute_jscore_my_ip_address);
    if (!function)
        goto jscoregtk_execute_error;

    g_proxy_execute_jscore.JSObjectSetProperty(global, g_proxy_execute_jscore.JSContextGetGlobalObject(global),
                                                  function_name, function, kJSPropertyAttributeNone, NULL);
    g_proxy_execute_jscore.JSStringRelease(function_name);
    function_name = NULL;

    // Load Mozilla's JavaScript PAC utilities to help process PAC files
    JSStringRef utils_javascript = g_proxy_execute_jscore.JSStringCreateWithUTF8CString(MOZILLA_PAC_JAVASCRIPT);
    if (!utils_javascript)
        goto jscoregtk_execute_error;
    g_proxy_execute_jscore.JSEvaluateScript(global, utils_javascript, NULL, NULL, 1, &exception);
    g_proxy_execute_jscore.JSStringRelease(utils_javascript);
    if (exception) {
        js_print_exception(global, exception);
        goto jscoregtk_execute_error;
    }

    // Load PAC script
    JSStringRef script_string = g_proxy_execute_jscore.JSStringCreateWithUTF8CString(script);
    g_proxy_execute_jscore.JSEvaluateScript(global, script_string, NULL, NULL, 1, &exception);
    g_proxy_execute_jscore.JSStringRelease(script_string);
    if (exception) {
        js_print_exception(global, exception);
        goto jscoregtk_execute_error;
    }

    // Construct the call FindProxyForURL
    char *host = parse_url_host(url);
    snprintf(find_proxy, sizeof(find_proxy), "FindProxyForURL(\"%s\", \"%s\");", url, host ? host : url);
    free(host);

    // Execute the call to FindProxyForURL
    JSStringRef find_proxy_string = g_proxy_execute_jscore.JSStringCreateWithUTF8CString(find_proxy);
    if (!find_proxy_string)
        goto jscoregtk_execute_error;
    JSValueRef proxy_value =
        g_proxy_execute_jscore.JSEvaluateScript(global, find_proxy_string, NULL, NULL, 1, &exception);
    g_proxy_execute_jscore.JSStringRelease(find_proxy_string);
    if (exception) {
        js_print_exception(global, exception);
        goto jscoregtk_execute_error;
    }

    if (!g_proxy_execute_jscore.JSValueIsString(global, proxy_value))
        goto jscoregtk_execute_error;

    // Get the result of the call to FindProxyForURL
    JSStringRef proxy_string = g_proxy_execute_jscore.JSValueToStringCopy(global, proxy_value, NULL);
    if (proxy_string) {
        proxy_execute->list = js_string_dup_to_utf8(proxy_string);
        g_proxy_execute_jscore.JSStringRelease(proxy_string);
    }

    success = true;

jscoregtk_execute_error:
jscoregtk_execute_cleanup:

    if (function_name)
        g_proxy_execute_jscore.JSStringRelease(function_name);

    if (global) {
        g_proxy_execute_jscore.JSGarbageCollect(global);
        g_proxy_execute_jscore.JSGlobalContextRelease(global);
    }

    return success;
}

const char *proxy_execute_jscore_get_list(void *ctx) {
    proxy_execute_jscore_s *proxy_execute = (proxy_execute_jscore_s *)ctx;
    return proxy_execute->list;
}

bool proxy_execute_jscore_get_error(void *ctx, int32_t *error) {
    proxy_execute_jscore_s *proxy_execute = (proxy_execute_jscore_s *)ctx;
    *error = proxy_execute->error;
    return true;
}

void *proxy_execute_jscore_create(void) {
    proxy_execute_jscore_s *proxy_execute =
        (proxy_execute_jscore_s *)calloc(1, sizeof(proxy_execute_jscore_s));
    return proxy_execute;
}

bool proxy_execute_jscore_delete(void **ctx) {
    proxy_execute_jscore_s *proxy_execute;
    if (!ctx)
        return false;
    proxy_execute = (proxy_execute_jscore_s *)*ctx;
    if (!proxy_execute)
        return false;
    free(proxy_execute->list);
    free(proxy_execute);
    *ctx = NULL;
    return true;
}

/*********************************************************************/

bool proxy_execute_jscore_init(void) {
#ifdef __APPLE__
    g_proxy_execute_jscore.module = dlopen("/System/Library/Frameworks/JavaScriptCore.framework/Versions/Current/JavaScriptCore", RTLD_LAZY | RTLD_LOCAL);
#else
    g_proxy_execute_jscore.module = dlopen("libjavascriptcoregtk-4.0.so.18", RTLD_LAZY | RTLD_LOCAL);
    if (g_proxy_execute_jscore.module == NULL)
        g_proxy_execute_jscore.module = dlopen("libjavascriptcoregtk-3.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (g_proxy_execute_jscore.module == NULL)
        g_proxy_execute_jscore.module = dlopen("libjavascriptcoregtk-1.0.so.0", RTLD_LAZY | RTLD_LOCAL);
#endif

    if (!g_proxy_execute_jscore.module)
        return false;

    // Class functions
    g_proxy_execute_jscore.JSClassCreate = dlsym(g_proxy_execute_jscore.module, "JSClassCreate");
    if (!g_proxy_execute_jscore.JSClassCreate)
        goto jscore_init_error;
    // Object functions
    g_proxy_execute_jscore.JSObjectMake = dlsym(g_proxy_execute_jscore.module, "JSObjectMake");
    if (!g_proxy_execute_jscore.JSObjectMake)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSObjectMakeFunctionWithCallback =
        dlsym(g_proxy_execute_jscore.module, "JSObjectMakeFunctionWithCallback");
    if (!g_proxy_execute_jscore.JSObjectMakeFunctionWithCallback)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSObjectGetProperty = dlsym(g_proxy_execute_jscore.module, "JSObjectGetProperty");
    if (!g_proxy_execute_jscore.JSObjectGetProperty)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSObjectSetProperty = dlsym(g_proxy_execute_jscore.module, "JSObjectSetProperty");
    if (!g_proxy_execute_jscore.JSObjectSetProperty)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSObjectGetPrivate = dlsym(g_proxy_execute_jscore.module, "JSObjectGetPrivate");
    if (!g_proxy_execute_jscore.JSObjectGetPrivate)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSObjectSetPrivate = dlsym(g_proxy_execute_jscore.module, "JSObjectSetPrivate");
    if (!g_proxy_execute_jscore.JSObjectSetPrivate)
        goto jscore_init_error;
    // Context functions
    g_proxy_execute_jscore.JSContextGetGlobalObject =
        dlsym(g_proxy_execute_jscore.module, "JSContextGetGlobalObject");
    if (!g_proxy_execute_jscore.JSContextGetGlobalObject)
        goto jscore_init_error;
    // Value functions
    g_proxy_execute_jscore.JSValueIsString = dlsym(g_proxy_execute_jscore.module, "JSValueIsString");
    if (!g_proxy_execute_jscore.JSValueIsString)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSValueIsNumber = dlsym(g_proxy_execute_jscore.module, "JSValueIsNumber");
    if (!g_proxy_execute_jscore.JSValueIsNumber)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSValueToObject = dlsym(g_proxy_execute_jscore.module, "JSValueToObject");
    if (!g_proxy_execute_jscore.JSValueToObject)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSValueToStringCopy = dlsym(g_proxy_execute_jscore.module, "JSValueToStringCopy");
    if (!g_proxy_execute_jscore.JSValueToStringCopy)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSValueToNumber = dlsym(g_proxy_execute_jscore.module, "JSValueToNumber");
    if (!g_proxy_execute_jscore.JSValueToNumber)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSValueMakeNull = dlsym(g_proxy_execute_jscore.module, "JSValueMakeNull");
    if (!g_proxy_execute_jscore.JSValueMakeNull)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSValueMakeBoolean = dlsym(g_proxy_execute_jscore.module, "JSValueMakeBoolean");
    if (!g_proxy_execute_jscore.JSValueMakeBoolean)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSValueMakeString = dlsym(g_proxy_execute_jscore.module, "JSValueMakeString");
    if (!g_proxy_execute_jscore.JSValueMakeString)
        goto jscore_init_error;
    // String functions
    g_proxy_execute_jscore.JSStringCreateWithUTF8CString =
        dlsym(g_proxy_execute_jscore.module, "JSStringCreateWithUTF8CString");
    if (!g_proxy_execute_jscore.JSStringCreateWithUTF8CString)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSStringGetUTF8CString =
        dlsym(g_proxy_execute_jscore.module, "JSStringGetUTF8CString");
    if (!g_proxy_execute_jscore.JSStringGetUTF8CString)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSStringGetMaximumUTF8CStringSize =
        dlsym(g_proxy_execute_jscore.module, "JSStringGetMaximumUTF8CStringSize");
    if (!g_proxy_execute_jscore.JSStringGetMaximumUTF8CStringSize)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSStringGetMaximumUTF8CStringSize =
        dlsym(g_proxy_execute_jscore.module, "JSStringGetMaximumUTF8CStringSize");
    if (!g_proxy_execute_jscore.JSStringGetMaximumUTF8CStringSize)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSStringRelease = dlsym(g_proxy_execute_jscore.module, "JSStringRelease");
    if (!g_proxy_execute_jscore.JSStringRelease)
        goto jscore_init_error;
    // Global context functions
    g_proxy_execute_jscore.JSGlobalContextCreate = dlsym(g_proxy_execute_jscore.module, "JSGlobalContextCreate");
    if (!g_proxy_execute_jscore.JSGlobalContextCreate)
        goto jscore_init_error;
    g_proxy_execute_jscore.JSGlobalContextRelease =
        dlsym(g_proxy_execute_jscore.module, "JSGlobalContextRelease");
    if (!g_proxy_execute_jscore.JSGlobalContextRelease)
        goto jscore_init_error;
    // Execute functions
    g_proxy_execute_jscore.JSEvaluateScript = dlsym(g_proxy_execute_jscore.module, "JSEvaluateScript");
    if (!g_proxy_execute_jscore.JSEvaluateScript)
        goto jscore_init_error;
    // Garbage collection functions
    g_proxy_execute_jscore.JSGarbageCollect = dlsym(g_proxy_execute_jscore.module, "JSGarbageCollect");
    if (!g_proxy_execute_jscore.JSGarbageCollect)
        goto jscore_init_error;

    return true;

jscore_init_error:
    proxy_execute_jscore_uninit();
    return false;
}

bool proxy_execute_jscore_uninit(void) {
    if (g_proxy_execute_jscore.module)
        dlclose(g_proxy_execute_jscore.module);

    memset(&g_proxy_execute_jscore, 0, sizeof(g_proxy_execute_jscore));
    return true;
}
