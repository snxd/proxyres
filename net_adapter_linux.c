#include "proxyres_config.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if HAVE_NETDB_H
#  include <netdb.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef HAVE_NET_IF_ARP_H
#  include <net/if_arp.h>
#else
#  define ARPHRD_ETHER   1  // Ethernet hardware format
#  define ARPHRD_IEEE802 6  // IEEE 802.2 Ethernet/TR/TB
#endif
#include <sys/ioctl.h>
#include <net/route.h>
#include <linux/rtnetlink.h>

#include "log.h"
#include "net_adapter.h"
#include "util.h"
#include "util_win.h"

// Get the IPv4 default gateway for an interface from the routing table
static bool net_adapter_get_gateway(const char *if_name, uint8_t gateway[4]) {
    char line[256];
    char name[IF_NAMESIZE];
    unsigned int dst = 0, gw = 0, flags = 0, mask = 0;
    int metric = 0, best_metric = 0;
    bool found = false;

    FILE *fp = fopen("/proc/net/route", "r");
    if (!fp)
        return false;

    // Skip the header line
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return false;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%15s %x %x %x %*d %*u %d %x", name, &dst, &gw, &flags, &metric, &mask) != 6)
            continue;
        if (strcmp(name, if_name) != 0 || dst != 0 || mask != 0)
            continue;
        if ((flags & (RTF_UP | RTF_GATEWAY)) != (RTF_UP | RTF_GATEWAY))
            continue;
        // Use the default route with the lowest metric
        if (found && metric >= best_metric)
            continue;

        // Parsed value keeps network byte order
        memcpy(gateway, &gw, sizeof(gw));
        best_metric = metric;
        found = true;
    }

    fclose(fp);
    return found;
}

// Get the interface carrying the best IPv4 route to the internet
static uint32_t net_adapter_primary_index(void) {
    struct {
        struct nlmsghdr hdr;
        struct rtmsg msg;
        struct rtattr dst_attr;
        uint32_t dst;
    } request = {{0}};
    struct {
        struct nlmsghdr hdr;
        uint8_t data[4096];
    } reply = {{0}};
    struct timeval timeout = {1, 0};
    uint32_t index = 0;
    ssize_t len = 0;

    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd == -1)
        return 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    request.hdr.nlmsg_len = sizeof(request);
    request.hdr.nlmsg_type = RTM_GETROUTE;
    request.hdr.nlmsg_flags = NLM_F_REQUEST;
    request.msg.rtm_family = AF_INET;
    request.msg.rtm_dst_len = 32;

    // Any public address works since only the route is looked up
    request.dst_attr.rta_type = RTA_DST;
    request.dst_attr.rta_len = RTA_LENGTH(sizeof(request.dst));
    request.dst = htonl(0x08080808);

    if (send(fd, &request, request.hdr.nlmsg_len, 0) == (ssize_t)request.hdr.nlmsg_len)
        len = recv(fd, &reply, sizeof(reply), 0);
    close(fd);

    if (len <= 0 || !NLMSG_OK(&reply.hdr, len) || reply.hdr.nlmsg_type != RTM_NEWROUTE)
        return 0;

    // Find the outgoing interface of the route
    struct rtmsg *route = (struct rtmsg *)NLMSG_DATA(&reply.hdr);
    int32_t attr_len = RTM_PAYLOAD(&reply.hdr);
    for (struct rtattr *attr = RTM_RTA(route); RTA_OK(attr, attr_len); attr = RTA_NEXT(attr, attr_len)) {
        if (attr->rta_type == RTA_OIF)
            memcpy(&index, RTA_DATA(attr), sizeof(index));
    }
    return index;
}

bool net_adapter_enum(void *user_data, net_adapter_cb callback) {
    net_adapter_s adapter;
    struct ifaddrs *ifp = NULL;
    struct ifaddrs *ifa = NULL;
    uint32_t primary_index = 0;

    if (getifaddrs(&ifp) == -1)
        return false;

    primary_index = net_adapter_primary_index();

    for (ifa = ifp; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        // Ignore non-physical adapters
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        if ((ifa->ifa_flags & ARPHRD_ETHER) == 0 && (ifa->ifa_flags & ARPHRD_IEEE802) == 0)
            continue;

        memset(&adapter, 0, sizeof(adapter));

        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (fd) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));

            ifr.ifr_ifindex = if_nametoindex(ifa->ifa_name);

            // Copy adapter name
            if (ioctl(fd, SIOCGIFNAME, &ifr) == 0)
                strncat(adapter.name, ifr.ifr_name, sizeof(adapter.name) - 1);

            // Check if the connection is online
            if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
                if (ifr.ifr_flags & IFF_UP)
                    adapter.is_connected = true;
            }

            // Copy hardware address
            if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
                adapter.mac_length = IFHWADDRLEN;
                if (adapter.mac_length > sizeof(adapter.mac))
                    adapter.mac_length = sizeof(adapter.mac);
                memcpy(adapter.mac, ifr.ifr_hwaddr.sa_data, adapter.mac_length);
            }

            close(fd);
        } else {
            strncat(adapter.name, ifa->ifa_name, sizeof(adapter.name) - 1);
        }

        if (primary_index && if_nametoindex(ifa->ifa_name) == primary_index)
            adapter.is_primary = true;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            memcpy(adapter.ip, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, sizeof(adapter.ip));
            net_adapter_get_gateway(ifa->ifa_name, adapter.gateway);
            memcpy(adapter.netmask, &((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr, sizeof(adapter.netmask));
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            memcpy(adapter.ipv6, &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr, sizeof(adapter.ipv6));
            memcpy(adapter.netmaskv6, &((struct sockaddr_in6 *)ifa->ifa_netmask)->sin6_addr, sizeof(adapter.netmaskv6));
            adapter.is_ipv6 = true;
        }

        if (!callback(user_data, &adapter))
            break;
    }

    freeifaddrs(ifp);
    return true;
}
