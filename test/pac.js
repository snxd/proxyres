function FindProxyForURL(url, host) {
  if (isPlainHostName(host) || host == "127.0.0.1") {
    return "DIRECT";
  }
  // Explicit port numbers are needed to prevent default to 80.
  if (host == "simple.com") {
    return "PROXY no-such-proxy:80";
  } else if (host == "multi.com") {
    return "HTTPS some-such-proxy:443; HTTPS any-such-proxy:41";
  } else if (host == "multi-legacy.com") {
    // Older implementations don't support HTTP/HTTPS/SOCKS prefixes.
    return "PROXY some-such-proxy:443; PROXY any-such-proxy:41";
  }
  return "DIRECT";
}
