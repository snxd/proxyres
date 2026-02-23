#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DHCP_SERVER_PORT    (67)
#define DHCP_CLIENT_PORT    (68)

#define DHCP_MAGIC          ("\x63\x82\x53\x63")
#define DHCP_MAGIC_LEN      (4)

#define DHCP_ACK            (5)
#define DHCP_INFORM         (8)

#define DHCP_BOOT_REQUEST   (1)
#define DHCP_BOOT_REPLY     (2)

#define DHCP_OPT_PAD        (0)
#define DHCP_OPT_MSGTYPE    (0x35)
#define DHCP_OPT_PARAMREQ   (0x37)
#define DHCP_OPT_WPAD       (0xfc)
#define DHCP_OPT_END        (0xff)

#define DHCP_OPT_MIN_LENGTH (312)

#define ETHERNET_TYPE       (1)
#define ETHERNET_LENGTH     (6)

typedef struct dhcp_msg {
    uint8_t op;         /* operation */
    uint8_t htype;      /* hardware address type */
    uint8_t hlen;       /* hardware address len */
    uint8_t hops;       /* message hops */
    uint32_t xid;       /* transaction id */
    uint16_t secs;      /* seconds since protocol start */
    uint16_t flags;     /* 0 = unicast, 1 = broadcast */
    uint32_t ciaddr;    /* client IP */
    uint32_t yiaddr;    /* your IP */
    uint32_t siaddr;    /* server IP */
    uint32_t giaddr;    /* gateway IP */
    uint8_t chaddr[16]; /* client hardware address */
    uint8_t sname[64];  /* server name */
    uint8_t file[128];  /* bootstrap file */
    uint8_t options[DHCP_OPT_MIN_LENGTH];
} dhcp_msg;

typedef struct dhcp_option {
    uint8_t type;
    uint8_t length;
    uint8_t value[1];
} dhcp_option;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PROXYRES_TESTING
bool dhcp_check_magic(uint8_t *options);
uint8_t *dhcp_copy_magic(uint8_t *options);
uint8_t *dhcp_copy_option(uint8_t *options, dhcp_option *option);
uint8_t *dhcp_get_option(dhcp_msg *reply, uint8_t type, uint8_t *length);
#endif

#ifdef __cplusplus
}
#endif
