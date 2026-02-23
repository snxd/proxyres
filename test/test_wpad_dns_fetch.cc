#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include "wpad_dns.h"

static const char *mock_target_url = nullptr;
static const char *mock_pac_script = nullptr;

static char *mock_fetch_returns_script(const char *url, int32_t *error) {
    if (mock_target_url && strcmp(url, mock_target_url) == 0)
        return strdup(mock_pac_script);
    if (error)
        *error = -1;
    return nullptr;
}

static char *mock_fetch_all_fail(const char *url, int32_t *error) {
    (void)url;
    if (error)
        *error = -1;
    return nullptr;
}

static int mock_fetch_call_count = 0;

static char *mock_fetch_first_succeeds(const char *url, int32_t *error) {
    (void)url;
    mock_fetch_call_count++;
    if (mock_fetch_call_count == 1)
        return strdup("function FindProxyForURL() { return \"DIRECT\"; }");
    if (error)
        *error = -1;
    return nullptr;
}

class wpad_dns_fetch : public ::testing::Test {
   protected:
    void SetUp() override {
        mock_fetch_call_count = 0;
    }
    void TearDown() override {
        wpad_dns_set_fetch_func(nullptr);

        mock_target_url = nullptr;
        mock_pac_script = nullptr;
    }
};

TEST_F(wpad_dns_fetch, mock_returns_script) {
    mock_target_url = "http://wpad.host.example.com/wpad.dat";
    mock_pac_script = "function FindProxyForURL() { return \"PROXY proxy:8080\"; }";
    wpad_dns_set_fetch_func(mock_fetch_returns_script);

    char *result = wpad_dns("host.example.com");
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, mock_pac_script);
    free(result);
}

TEST_F(wpad_dns_fetch, mock_all_fail) {
    wpad_dns_set_fetch_func(mock_fetch_all_fail);

    char *result = wpad_dns("host.example.com");
    EXPECT_EQ(result, nullptr);
}

TEST_F(wpad_dns_fetch, mock_first_url_succeeds) {
    wpad_dns_set_fetch_func(mock_fetch_first_succeeds);

    char *result = wpad_dns("host.sub.example.com");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(mock_fetch_call_count, 1);
    free(result);
}
