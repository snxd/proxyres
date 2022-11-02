#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gtest/gtest.h>

extern "C" {
#include "config.h"
#include "log.h"
}

TEST(config, override_auto_config_url) {
    proxy_config_set_auto_config_url_override("http://127.0.0.1:8000/wpad.dat");
    char *auto_config_url = proxy_config_get_auto_config_url();
    EXPECT_NE(auto_config_url, nullptr);
    if (auto_config_url) {
        EXPECT_STREQ(auto_config_url, "http://127.0.0.1:8000/wpad.dat");
        free(auto_config_url);
    }
}

TEST(config, override_proxy) {
    proxy_config_set_proxy_override("http://127.0.0.1:8000/");
    char *proxy = proxy_config_get_proxy("http");
    EXPECT_NE(proxy, nullptr);
    if (proxy) {
        EXPECT_STREQ(proxy, "http://127.0.0.1:8000/");
        free(proxy);
    }
}

TEST(config, override_bypass_list) {
    proxy_config_set_bypass_list_override("<local>");
    char *bypass_list = proxy_config_get_bypass_list();
    EXPECT_NE(bypass_list, nullptr);
    if (bypass_list) {
        EXPECT_STREQ(bypass_list, "<local>");
        free(bypass_list);
    }
}