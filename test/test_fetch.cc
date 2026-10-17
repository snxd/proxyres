#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gtest/gtest.h>

#include "fetch.h"

TEST(fetch, get) {
    int32_t error = 0;
#if defined(HAVE_CURL) || defined(PROXYRES_EXECUTE_PAC)
    const char *url = "https://google.com/";
    const char *expected_string = "world's information";
#else
    const char *url = "http://google.com/";
    const char *expected_string = "document has moved";
#endif
    char *body = fetch_get(url, &error);
    EXPECT_EQ(error, 0);
    EXPECT_NE(body, nullptr);
    if (body) {
        EXPECT_TRUE(strstr(body, expected_string) != nullptr);
        free(body);
    }
}

#if defined(PROXYRES_EXECUTE_PAC) && !defined(HAVE_CURL)
TEST(fetch, rejects_non_http_scheme) {
    const char *urls[] = {"file:///etc/hosts", "data:text/plain,DIRECT", "ftp://example.com/proxy.pac", "not a url"};
    for (const char *url : urls) {
        int32_t error = 0;
        EXPECT_EQ(fetch_get(url, &error), nullptr) << url;
        EXPECT_EQ(error, EINVAL) << url;
    }
}
#endif
