#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include "wpad_dhcp_posix_p.h"

// Build a minimal DHCP ACK message with magic and given options
static void build_dhcp_reply(dhcp_msg *msg, const uint8_t *opts_data, size_t opts_len) {
    memset(msg, 0, sizeof(*msg));
    msg->op = DHCP_BOOT_REPLY;
    // Copy magic
    memcpy(msg->options, DHCP_MAGIC, DHCP_MAGIC_LEN);
    // Copy options after magic
    if (opts_data && opts_len > 0)
        memcpy(msg->options + DHCP_MAGIC_LEN, opts_data, opts_len);
}

TEST(dhcp_parse, get_option_wpad) {
    const char *url = "http://wpad.example.com/wpad.dat";
    uint8_t url_len = (uint8_t)strlen(url);

    // Build options: type=WPAD, length, value, END
    uint8_t opts[256];
    size_t pos = 0;
    opts[pos++] = DHCP_OPT_WPAD;
    opts[pos++] = url_len;
    memcpy(opts + pos, url, url_len);
    pos += url_len;
    opts[pos++] = DHCP_OPT_END;

    dhcp_msg msg;
    build_dhcp_reply(&msg, opts, pos);

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_WPAD, &length);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(length, url_len);
    EXPECT_STREQ((char *)value, url);
    free(value);
}

TEST(dhcp_parse, get_option_missing) {
    // Message with only MSGTYPE option, no WPAD
    uint8_t opts[] = {DHCP_OPT_MSGTYPE, 1, DHCP_ACK, DHCP_OPT_END};

    dhcp_msg msg;
    build_dhcp_reply(&msg, opts, sizeof(opts));

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_WPAD, &length);
    EXPECT_EQ(value, nullptr);
}

TEST(dhcp_parse, get_option_with_padding) {
    const char *url = "http://wpad.test/wpad.dat";
    uint8_t url_len = (uint8_t)strlen(url);

    // PAD PAD PAD, then WPAD option, then END
    uint8_t opts[256];
    size_t pos = 0;
    opts[pos++] = DHCP_OPT_PAD;
    opts[pos++] = DHCP_OPT_PAD;
    opts[pos++] = DHCP_OPT_PAD;
    opts[pos++] = DHCP_OPT_WPAD;
    opts[pos++] = url_len;
    memcpy(opts + pos, url, url_len);
    pos += url_len;
    opts[pos++] = DHCP_OPT_END;

    dhcp_msg msg;
    build_dhcp_reply(&msg, opts, pos);

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_WPAD, &length);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(length, url_len);
    EXPECT_STREQ((char *)value, url);
    free(value);
}

TEST(dhcp_parse, get_option_msgtype_ack) {
    uint8_t opts[] = {DHCP_OPT_MSGTYPE, 1, DHCP_ACK, DHCP_OPT_END};

    dhcp_msg msg;
    build_dhcp_reply(&msg, opts, sizeof(opts));

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_MSGTYPE, &length);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(length, 1);
    EXPECT_EQ(*value, DHCP_ACK);
    free(value);
}

TEST(dhcp_parse, check_magic_valid) {
    uint8_t options[DHCP_OPT_MIN_LENGTH] = {0};
    memcpy(options, DHCP_MAGIC, DHCP_MAGIC_LEN);
    EXPECT_TRUE(dhcp_check_magic(options));
}

TEST(dhcp_parse, check_magic_invalid) {
    uint8_t options[DHCP_OPT_MIN_LENGTH] = {0};
    options[0] = 0x00;
    options[1] = 0x00;
    options[2] = 0x00;
    options[3] = 0x00;
    EXPECT_FALSE(dhcp_check_magic(options));
}

TEST(dhcp_parse, construct_inform_options) {
    uint8_t buf[64] = {0};
    uint8_t *pos = buf;

    // Copy magic
    pos = dhcp_copy_magic(pos);
    EXPECT_EQ(pos - buf, DHCP_MAGIC_LEN);
    EXPECT_EQ(memcmp(buf, DHCP_MAGIC, DHCP_MAGIC_LEN), 0);

    // Copy MSGTYPE option
    dhcp_option opt_msg = {DHCP_OPT_MSGTYPE, 1, {DHCP_INFORM}};
    pos = dhcp_copy_option(pos, &opt_msg);
    // Expected type(1) + length(1) + value(1) = 3 bytes after magic
    EXPECT_EQ(pos - buf, DHCP_MAGIC_LEN + 3);
    EXPECT_EQ(buf[DHCP_MAGIC_LEN], DHCP_OPT_MSGTYPE);
    EXPECT_EQ(buf[DHCP_MAGIC_LEN + 1], 1);
    EXPECT_EQ(buf[DHCP_MAGIC_LEN + 2], DHCP_INFORM);

    // Copy END option
    dhcp_option opt_end = {DHCP_OPT_END, 0, {0}};
    pos = dhcp_copy_option(pos, &opt_end);
    EXPECT_EQ(buf[DHCP_MAGIC_LEN + 3], DHCP_OPT_END);
    EXPECT_EQ(buf[DHCP_MAGIC_LEN + 4], 0);  // length of END
}

TEST(dhcp_parse, get_option_truncated_at_length) {
    // Option type present but length byte is past the end of the buffer.
    // Place the type byte at the very last position in the options area.
    dhcp_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = DHCP_BOOT_REPLY;
    memcpy(msg.options, DHCP_MAGIC, DHCP_MAGIC_LEN);
    // Fill with padding up to the last byte, then place the option type
    size_t opts_avail = sizeof(msg.options) - DHCP_MAGIC_LEN;
    memset(msg.options + DHCP_MAGIC_LEN, DHCP_OPT_PAD, opts_avail - 1);
    msg.options[sizeof(msg.options) - 1] = DHCP_OPT_WPAD;

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_WPAD, &length);
    EXPECT_EQ(value, nullptr);
}

TEST(dhcp_parse, get_option_truncated_at_value) {
    // Option type and length are present but claimed data extends past the buffer.
    // Place option near the end so only 3 bytes remain for data but length says 10.
    dhcp_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = DHCP_BOOT_REPLY;
    memcpy(msg.options, DHCP_MAGIC, DHCP_MAGIC_LEN);
    size_t opts_avail = sizeof(msg.options) - DHCP_MAGIC_LEN;
    // Fill with padding, leaving room for type(1) + length(1) + 3 bytes of data
    size_t pad_len = opts_avail - 5;
    memset(msg.options + DHCP_MAGIC_LEN, DHCP_OPT_PAD, pad_len);
    uint8_t *p = msg.options + DHCP_MAGIC_LEN + pad_len;
    p[0] = DHCP_OPT_WPAD;
    p[1] = 10;  // claims 10 bytes but only 3 remain
    p[2] = 'a';
    p[3] = 'b';
    p[4] = 'c';

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_WPAD, &length);
    EXPECT_EQ(value, nullptr);
}

TEST(dhcp_parse, get_option_no_end_marker) {
    // Options buffer filled with padding, no END marker — should not overrun
    dhcp_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = DHCP_BOOT_REPLY;
    memcpy(msg.options, DHCP_MAGIC, DHCP_MAGIC_LEN);
    memset(msg.options + DHCP_MAGIC_LEN, DHCP_OPT_PAD, sizeof(msg.options) - DHCP_MAGIC_LEN);

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_WPAD, &length);
    EXPECT_EQ(value, nullptr);
}

TEST(dhcp_parse, get_option_skip_truncated_preceding) {
    // First option has truncated data, target option follows — should not be found
    uint8_t opts[] = {DHCP_OPT_MSGTYPE, 20, DHCP_ACK,  // claims 20 bytes, only 1 present
                      DHCP_OPT_WPAD,    3,  'u',      'r', 'l', DHCP_OPT_END};

    dhcp_msg msg;
    build_dhcp_reply(&msg, opts, sizeof(opts));

    uint8_t length = 0;
    uint8_t *value = dhcp_get_option(&msg, DHCP_OPT_WPAD, &length);
    EXPECT_EQ(value, nullptr);
}

TEST(dhcp_parse, full_ack_with_wpad) {
    // Build a realistic DHCP ACK with both MSGTYPE and WPAD options
    const char *url = "http://wpad.corp.example.com/wpad.dat";
    uint8_t url_len = (uint8_t)strlen(url);

    uint8_t opts[256];
    size_t pos = 0;

    // MSGTYPE = ACK
    opts[pos++] = DHCP_OPT_MSGTYPE;
    opts[pos++] = 1;
    opts[pos++] = DHCP_ACK;

    // WPAD URL
    opts[pos++] = DHCP_OPT_WPAD;
    opts[pos++] = url_len;
    memcpy(opts + pos, url, url_len);
    pos += url_len;

    opts[pos++] = DHCP_OPT_END;

    dhcp_msg msg;
    build_dhcp_reply(&msg, opts, pos);

    // Mimic validation path
    uint8_t opt_length = 0;
    uint8_t *opt = dhcp_get_option(&msg, DHCP_OPT_MSGTYPE, &opt_length);
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt_length, 1);
    EXPECT_EQ(*opt, DHCP_ACK);
    free(opt);

    opt = dhcp_get_option(&msg, DHCP_OPT_WPAD, &opt_length);
    ASSERT_NE(opt, nullptr);
    EXPECT_EQ(opt_length, url_len);
    EXPECT_STREQ((char *)opt, url);
    free(opt);
}
