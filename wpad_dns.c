#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netdb.h>
#  include <unistd.h>
#endif

#include "fetch.h"
#include "log.h"
#include "net_util.h"
#include "util.h"
#include "wpad_dns.h"

#ifdef _WIN32
#  define socketerr WSAGetLastError()
#else
#  define socketerr errno
#endif

#ifdef PROXYRES_TESTING
static wpad_dns_fetch_func wpad_dns_fetch = fetch_get;
#endif

// Generate NULL-terminated array of WPAD URLs from an FQDN
char **wpad_dns_get_urls(const char *fqdn, int32_t *count) {
    if (!fqdn || !*fqdn)
        return NULL;

    // Count the number of dots to determine max url count
    int32_t max_urls = 0;
    const char *name = fqdn;
    while ((name = strchr(name, '.')) != NULL) {
        max_urls++;
        name++;
    }

    if (max_urls == 0)
        return NULL;

    // Allocate array (max_urls entries + NULL terminator)
    char **urls = (char **)calloc(max_urls + 1, sizeof(char *));
    if (!urls)
        return NULL;

    int32_t num_urls = 0;
    name = fqdn;
    while (num_urls < max_urls) {
        // Skip empty labels (consecutive dots or leading dot)
        while (*name == '.')
            name++;

        const char *dot = strchr(name, '.');
        if (!dot)
            break;

        const char *next_part = dot + 1;

        // Skip trailing dot (next_part is empty)
        if (!*next_part)
            break;

        // Compute suffix length, excluding any trailing dot
        size_t name_len = strlen(name);
        if (name[name_len - 1] == '.')
            name_len--;

        size_t wpad_url_len = 12 + name_len + 9 + 1;  // "http://wpad." + name + "/wpad.dat"
        urls[num_urls] = (char *)calloc(wpad_url_len, sizeof(char));
        if (!urls[num_urls]) {
            wpad_dns_free_urls(urls);
            return NULL;
        }
        snprintf(urls[num_urls], wpad_url_len, "http://wpad.%.*s/wpad.dat", (int)name_len, name);
        str_collapse_chr(urls[num_urls], '.');
        num_urls++;
        name = next_part;
    }

    if (num_urls == 0) {
        free(urls);
        return NULL;
    }

    urls[num_urls] = NULL;
    if (count)
        *count = num_urls;
    return urls;
}

// Free NULL-terminated array of WPAD URLs
void wpad_dns_free_urls(char **urls) {
    if (!urls)
        return;
    for (int32_t i = 0; urls[i]; i++)
        free(urls[i]);
    free(urls);
}

// Request WPAD script using DNS
char *wpad_dns(const char *fqdn) {
    char hostname[HOST_MAX] = {0};
    int32_t error = 0;

    if (!fqdn) {
        // Get local hostname
        if (gethostname(hostname, sizeof(hostname)) == -1) {
            log_error("Unable to get hostname (%d)", socketerr);
            return NULL;
        }
        hostname[sizeof(hostname) - 1] = 0;

        // Get hostent for local hostname
        struct hostent *localent = gethostbyname(hostname);
        if (!localent) {
            log_error("Unable to get hostent for %s (%d)", hostname, socketerr);
            return NULL;
        }

        // Canonical name found in h_name
        fqdn = localent->h_name;

        // Validate canonical name
        if (!fqdn || !strlen(fqdn) || !strcmp(fqdn, "localhost") || isdigit(*fqdn)) {
            log_debug("Unable to get canonical name for %s (%d)", hostname, socketerr);
            return NULL;
        }
    }

    char **urls = wpad_dns_get_urls(fqdn, NULL);
    if (!urls)
        return NULL;

    char *script = NULL;
    for (int32_t i = 0; urls[i]; i++) {
        log_info("Checking next WPAD URL: %s", urls[i]);
#ifdef PROXYRES_TESTING
        script = wpad_dns_fetch(urls[i], &error);
#else
        script = fetch_get(urls[i], &error);
#endif
        if (script)
            break;
        log_info("No server found at %s (%d)", urls[i], error);
    }

    wpad_dns_free_urls(urls);
    return script;
}

#ifdef PROXYRES_TESTING
void wpad_dns_set_fetch_func(wpad_dns_fetch_func func) {
    wpad_dns_fetch = func ? func : fetch_get;
}
#endif