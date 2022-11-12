#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "log.h"
#include "net_adapter.h"
#include "util.h"
#include "util_socket.h"
#include "util_win.h"

#include <windows.h>
#include <iptypes.h>
#include <iphlpapi.h>

bool net_adapter_enum(void *user_data, net_adapter_cb callback) {
    IP_ADAPTER_ADDRESSES *adapter_addresses = NULL;
    IP_ADAPTER_GATEWAY_ADDRESS *gateway_address = NULL;
    IP_ADAPTER_UNICAST_ADDRESS *unicast_address = NULL;
    IP_ADAPTER_DNS_SERVER_ADDRESS_XP *dns_address = NULL;
    IP_ADAPTER_PREFIX *prefix = NULL;
    ULONG buffer_size = 0;
    ULONG required_size = 0;
    ULONG error = 0;
    net_adapter_s adapter = {0};
    uint8_t *buffer = NULL;
    int32_t cur_index = 0;
    int32_t found_adapter_index = 0;
    uint32_t min_index = UINT32_MAX;
    bool found = false;

    if (!callback)
        return false;

    error = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS, 0, NULL, &buffer_size);
    if (error != ERROR_SUCCESS && error != ERROR_BUFFER_OVERFLOW) {
        LOG_ERROR("Unable to allocate adapter info (%d)\n", error);
        return false;
    }

    buffer_size += 32;  // Prevent GetAdaptersAddresses overflow by 1 byte
    buffer = calloc(buffer_size, sizeof(uint8_t));
    required_size = buffer_size;

    error = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS, 0,
                                 (IP_ADAPTER_ADDRESSES *)buffer, &required_size);
    if (error != ERROR_SUCCESS) {
        LOG_ERROR("Unable to get adapter info (%d / %d:%d)\n", error, buffer_size, required_size);
        goto net_adapter_cleanup;
    }

    // Enumerate each adapter returned by the operating system
    adapter_addresses = (IP_ADAPTER_ADDRESSES *)buffer;
    do {
        memset(&adapter, 0, sizeof(adapter));

        // Populate adapter address
        adapter.mac_length = sizeof(adapter.mac);
        if (adapter.mac_length > adapter_addresses->PhysicalAddressLength)
            adapter.mac_length = (uint8_t)adapter_addresses->PhysicalAddressLength;
        memcpy(adapter.mac, adapter_addresses->PhysicalAddress, adapter.mac_length);

        // Populate adapter type and connection state flags
        if (adapter_addresses->OperStatus == IfOperStatusUp)
            adapter.is_connected = true;
        if ((adapter_addresses->IfType == IF_TYPE_ETHERNET_CSMACD) ||
            (adapter_addresses->IfType == IF_TYPE_IEEE80211))
            adapter.is_ethernet = true;
        if (adapter_addresses->Flags & IP_ADAPTER_DHCP_ENABLED && adapter_addresses->Dhcpv4Enabled &&
            adapter_addresses->Dhcpv4Server.iSockaddrLength >= sizeof(adapter.dhcp))
            adapter.is_dhcp_v4 = true;
        if (adapter_addresses->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            adapter.is_loopback = true;

        // Populate adapter name and description
        char *friendly_name = wchar_dup_to_utf8(adapter_addresses->FriendlyName);
        if (friendly_name)
            strncat(adapter.name, friendly_name, sizeof(adapter.name) - 1);
        free(friendly_name);

        char *description = wchar_dup_to_utf8(adapter_addresses->Description);
        if (description)
            strncat(adapter.description, description, sizeof(adapter.description) - 1);
        free(description);

        found_adapter_index = adapter_addresses->IfIndex;

        // Populate DHCPv4 server
        if (adapter.is_dhcp_v4)
            memcpy(adapter.dhcp, &adapter_addresses->Dhcpv4Server.lpSockaddr->sa_data[2], sizeof(adapter.dhcp));

        // Populate adapter ip address
        unicast_address = adapter_addresses->FirstUnicastAddress;
        while (unicast_address) {
            memcpy(adapter.ip, &unicast_address->Address.lpSockaddr->sa_data[2], sizeof(adapter.ip));
            break;
        }

        // Populate adapter netmask
        prefix = adapter_addresses->FirstPrefix;
        while (prefix) {
            memcpy(adapter.netmask, &prefix->Address.lpSockaddr->sa_data[2], sizeof(adapter.netmask));
            prefix = prefix->Next;
        }

        // Populate adapter dns servers
        dns_address = adapter_addresses->FirstDnsServerAddress;
        while (dns_address) {
            if (*adapter.primary_dns == 0) {
                memcpy(adapter.primary_dns, &dns_address->Address.lpSockaddr->sa_data[2],
                        sizeof(adapter.primary_dns));
            } else if (*adapter.secondary_dns == 0) {
                memcpy(adapter.secondary_dns, &dns_address->Address.lpSockaddr->sa_data[2],
                        sizeof(adapter.secondary_dns));
            }
            dns_address = dns_address->Next;
        }

        // Populate adapter gateway
        gateway_address = adapter_addresses->FirstGatewayAddress;
        while (gateway_address) {
            memcpy(adapter.gateway, &gateway_address->Address.lpSockaddr->sa_data[2], sizeof(adapter.gateway));
            gateway_address = gateway_address->Next;
            break;
        }

        if (callback(user_data, &adapter))
            break;

        adapter_addresses = adapter_addresses->Next;
    } while (adapter_addresses);

net_adapter_cleanup:
    free(buffer);
    return true;
}
