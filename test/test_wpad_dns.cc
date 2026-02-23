#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include "wpad_dns.h"

TEST(wpad_dns_urls, three_level_fqdn) {
    int32_t count = 0;
    char **urls = wpad_dns_get_urls("host.sub.example.com", &count);
    ASSERT_NE(urls, nullptr);
    ASSERT_EQ(count, 3);
    EXPECT_STREQ(urls[0], "http://wpad.host.sub.example.com/wpad.dat");
    EXPECT_STREQ(urls[1], "http://wpad.sub.example.com/wpad.dat");
    EXPECT_STREQ(urls[2], "http://wpad.example.com/wpad.dat");
    EXPECT_EQ(urls[3], nullptr);
    wpad_dns_free_urls(urls);
}

TEST(wpad_dns_urls, two_level_fqdn) {
    int32_t count = 0;
    char **urls = wpad_dns_get_urls("example.com", &count);
    ASSERT_NE(urls, nullptr);
    ASSERT_EQ(count, 1);
    EXPECT_STREQ(urls[0], "http://wpad.example.com/wpad.dat");
    EXPECT_EQ(urls[1], nullptr);
    wpad_dns_free_urls(urls);
}

TEST(wpad_dns_urls, single_label) {
    char **urls = wpad_dns_get_urls("localhost", nullptr);
    EXPECT_EQ(urls, nullptr);
}

TEST(wpad_dns_urls, null_input) {
    char **urls = wpad_dns_get_urls(nullptr, nullptr);
    EXPECT_EQ(urls, nullptr);
}

TEST(wpad_dns_urls, empty_string) {
    char **urls = wpad_dns_get_urls("", nullptr);
    EXPECT_EQ(urls, nullptr);
}

TEST(wpad_dns_urls, trailing_dot) {
    int32_t count = 0;
    char **urls = wpad_dns_get_urls("host.example.com.", &count);
    ASSERT_NE(urls, nullptr);
    ASSERT_EQ(count, 2);
    EXPECT_STREQ(urls[0], "http://wpad.host.example.com/wpad.dat");
    EXPECT_STREQ(urls[1], "http://wpad.example.com/wpad.dat");
    EXPECT_EQ(urls[2], nullptr);
    wpad_dns_free_urls(urls);
}

TEST(wpad_dns_urls, leading_dot) {
    // Leading dot is skipped, so ".example.com" behaves like "example.com"
    int32_t count = 0;
    char **urls = wpad_dns_get_urls(".example.com", &count);
    ASSERT_NE(urls, nullptr);
    ASSERT_EQ(count, 1);
    EXPECT_STREQ(urls[0], "http://wpad.example.com/wpad.dat");
    EXPECT_EQ(urls[1], nullptr);
    wpad_dns_free_urls(urls);
}

TEST(wpad_dns_urls, only_dots) {
    // All empty labels, nothing to generate
    char **urls = wpad_dns_get_urls("...", nullptr);
    EXPECT_EQ(urls, nullptr);
}

TEST(wpad_dns_urls, single_trailing_dot) {
    char **urls = wpad_dns_get_urls(".", nullptr);
    EXPECT_EQ(urls, nullptr);
}

TEST(wpad_dns_urls, deeply_nested) {
    int32_t count = 0;
    char **urls = wpad_dns_get_urls("a.b.c.d.e.com", &count);
    ASSERT_NE(urls, nullptr);
    ASSERT_EQ(count, 5);
    EXPECT_STREQ(urls[0], "http://wpad.a.b.c.d.e.com/wpad.dat");
    EXPECT_STREQ(urls[1], "http://wpad.b.c.d.e.com/wpad.dat");
    EXPECT_STREQ(urls[2], "http://wpad.c.d.e.com/wpad.dat");
    EXPECT_STREQ(urls[3], "http://wpad.d.e.com/wpad.dat");
    EXPECT_STREQ(urls[4], "http://wpad.e.com/wpad.dat");
    EXPECT_EQ(urls[5], nullptr);
    wpad_dns_free_urls(urls);
}
