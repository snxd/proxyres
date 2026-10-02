#include "proxyres_config.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef HAVE_NET_IF_ARP_H
#  include <net/if_arp.h>
#else
#  define ARPHRD_ETHER   1  // Ethernet hardware format
#  define ARPHRD_IEEE802 6  // Token-ring hardware format
#endif
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"
#include "net_adapter.h"
#include "util.h"
#include "util_win.h"

// Round up socket address length to the alignment used in routing messages
#define ROUNDUP(a) ((a) > 0 ? (1 + (((a) - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t))

// Get the IPv4 gateway of a routing message if it is an IPv4 default route
static struct sockaddr_in *net_adapter_route_gateway(struct rt_msghdr *rtm) {
    struct sockaddr *addrs[RTAX_MAX] = {0};
    char *sa = (char *)(rtm + 1);

    // Collect socket addresses that follow the message header
    for (int32_t i = 0; i < RTAX_MAX; i++) {
        if (rtm->rtm_addrs & (1 << i)) {
            addrs[i] = (struct sockaddr *)sa;
            sa += ROUNDUP(addrs[i]->sa_len);
        }
    }

    struct sockaddr_in *dst = (struct sockaddr_in *)addrs[RTAX_DST];
    struct sockaddr_in *gw = (struct sockaddr_in *)addrs[RTAX_GATEWAY];
    struct sockaddr_in mask = {0};

    // Host routes have no netmask and are never default routes
    if (rtm->rtm_flags & RTF_HOST)
        return NULL;

    // Netmask may be truncated to its non-zero bytes
    if (addrs[RTAX_NETMASK]) {
        size_t mask_len = addrs[RTAX_NETMASK]->sa_len;
        memcpy(&mask, addrs[RTAX_NETMASK], mask_len < sizeof(mask) ? mask_len : sizeof(mask));
    }

    // Only use default routes with an IPv4 gateway
    if (!dst || dst->sin_family != AF_INET || dst->sin_addr.s_addr != INADDR_ANY)
        return NULL;
    if (mask.sin_addr.s_addr != INADDR_ANY)
        return NULL;
    if (!gw || gw->sin_family != AF_INET)
        return NULL;
    return gw;
}

// Get the IPv4 default gateway for an interface from the routing table
static bool net_adapter_get_gateway(uint32_t if_index, uint8_t gateway[4]) {
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY};
    size_t buffer_len = 0;
    char *buffer = NULL;
    bool found = false;

    if (sysctl(mib, 6, NULL, &buffer_len, NULL, 0) < 0 || !buffer_len)
        return false;
    buffer = (char *)malloc(buffer_len);
    if (!buffer)
        return false;
    if (sysctl(mib, 6, buffer, &buffer_len, NULL, 0) < 0)
        buffer_len = 0;

    for (char *next = buffer; !found && next < buffer + buffer_len;) {
        struct rt_msghdr *rtm = (struct rt_msghdr *)next;

        if (!rtm->rtm_msglen)
            break;
        next += rtm->rtm_msglen;
        if (rtm->rtm_version != RTM_VERSION || rtm->rtm_index != if_index || !(rtm->rtm_flags & RTF_UP))
            continue;

        struct sockaddr_in *gw = net_adapter_route_gateway(rtm);
        if (!gw)
            continue;

        memcpy(gateway, &gw->sin_addr, sizeof(gw->sin_addr));
        found = true;
    }

    free(buffer);
    return found;
}

// Get the interface carrying the best IPv4 route to the internet
static uint32_t net_adapter_primary_index(void) {
    struct {
        struct rt_msghdr hdr;
        uint8_t data[512];
    } msg = {{0}};
    struct sockaddr_in *dst = (struct sockaddr_in *)msg.data;
    struct timeval timeout = {1, 0};
    const pid_t pid = getpid();
    ssize_t len = 0;

    int fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (fd == -1)
        return 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    msg.hdr.rtm_msglen = sizeof(msg.hdr) + sizeof(*dst);
    msg.hdr.rtm_version = RTM_VERSION;
    msg.hdr.rtm_type = RTM_GET;
    msg.hdr.rtm_addrs = RTA_DST;
    msg.hdr.rtm_pid = pid;
    msg.hdr.rtm_seq = 1;

    // Any public address works since only the route is looked up
    dst->sin_len = sizeof(*dst);
    dst->sin_family = AF_INET;
    dst->sin_addr.s_addr = htonl(0x08080808);

    if (write(fd, &msg, msg.hdr.rtm_msglen) == msg.hdr.rtm_msglen) {
        // Skip messages meant for other routing socket listeners
        do {
            len = read(fd, &msg, sizeof(msg));
        } while (len > 0 && (msg.hdr.rtm_pid != pid || msg.hdr.rtm_seq != 1));
    }
    close(fd);

    if (len <= 0 || msg.hdr.rtm_errno)
        return 0;
    return msg.hdr.rtm_index;
}

bool net_adapter_enum(void *user_data, net_adapter_cb callback) {
    net_adapter_s adapter;
    struct ifaddrs *ifp = NULL;
    struct ifaddrs *ifa = NULL;
    char *buffer = NULL;
    size_t buffer_len = 0;
    size_t required_len = 0;
    uint32_t primary_index = 0;

    if (getifaddrs(&ifp) == -1)
        return false;

    primary_index = net_adapter_primary_index();

    for (ifa = ifp; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;

        memset(&adapter, 0, sizeof(adapter));

        int mib[6] = {0};

        mib[0] = CTL_NET;
        mib[1] = AF_ROUTE;
        mib[2] = 0;
        mib[3] = AF_LINK;
        mib[4] = NET_RT_IFLIST;
        mib[5] = if_nametoindex(ifa->ifa_name);

        required_len = buffer_len;
        if (sysctl(mib, 6, NULL, &required_len, NULL, 0) < 0)
            continue;

        if (required_len > buffer_len) {
            free(buffer);
            buffer_len = required_len;
            buffer = (char *)malloc(buffer_len + 1);
        }

        if (sysctl(mib, 6, buffer, &buffer_len, NULL, 0) < 0)
            continue;

        struct if_msghdr *ifm = (struct if_msghdr *)buffer;

        // Ignore non-physical adapters
        if (ifm->ifm_flags & IFF_LOOPBACK)
            continue;
        if ((ifm->ifm_data.ifi_type & ARPHRD_ETHER) == 0 && (ifm->ifm_data.ifi_type & ARPHRD_IEEE802) == 0)
            continue;

        if (ifm->ifm_flags & IFF_UP)
            adapter.is_connected = true;
        // Interface indexes start at 1 so a failed route lookup never matches
        adapter.is_primary = ifm->ifm_index == primary_index;

        strncat(adapter.name, ifa->ifa_name, sizeof(adapter.name) - 1);
        adapter.mac_length = 6;
        memcpy(adapter.mac, LLADDR((struct sockaddr_dl *)(ifm + 1)), adapter.mac_length);

        if (ifa->ifa_addr->sa_family == AF_INET) {
            memcpy(adapter.ip, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, sizeof(adapter.ip));
            net_adapter_get_gateway((uint32_t)mib[5], adapter.gateway);
            memcpy(adapter.netmask, &((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr, sizeof(adapter.netmask));
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            memcpy(adapter.ipv6, &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr, sizeof(adapter.ipv6));
            memcpy(adapter.netmaskv6, &((struct sockaddr_in6 *)ifa->ifa_netmask)->sin6_addr, sizeof(adapter.netmaskv6));
            adapter.is_ipv6 = true;
        }

        if (!callback(user_data, &adapter))
            break;
    }

    free(buffer);

    freeifaddrs(ifp);
    return true;
}
