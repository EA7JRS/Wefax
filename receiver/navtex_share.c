/*
 * Wefax · sharing one receiver with Navtex (see navtex_share.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "navtex_share.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int navtex_parse_base(const char *base, char *host, int host_size, char *port, int port_size)
{
    const char *p, *colon;
    size_t n;
    if (base == NULL || strncmp(base, "http://", 7) != 0) return -1;
    p = base + 7;
    n = strcspn(p, "/");
    if (n == 0 || p[n] != '\0') return -1;                /* no path: only the address */
    colon = memchr(p, ':', n);
    if (colon != NULL) {
        size_t hn = (size_t) (colon - p), pn = n - hn - 1;
        if (hn == 0 || pn == 0 || pn > 5 || strspn(colon + 1, "0123456789") != pn || hn >= (size_t) host_size || pn >= (size_t) port_size) return -1;
        memcpy(host, p, hn); host[hn] = '\0';
        memcpy(port, colon + 1, pn); port[pn] = '\0';
    } else {
        if (n >= (size_t) host_size || port_size < 3) return -1;
        memcpy(host, p, n); host[n] = '\0';
        strcpy(port, "80");
    }
    return 0;
}

int navtex_set_receiver(const char *base, int rx, int on)
{
    char host[96], port[8], req[320], ans[256];
    struct addrinfo hints, *res = NULL, *a;
    struct timeval tv = { 5, 0 };
    int fd = -1, ok = -1;
    ssize_t n;

    if (navtex_parse_base(base, host, sizeof(host), port, sizeof(port)) != 0 || (rx != 1 && rx != 2)) return -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    for (a = res; a != NULL && fd < 0; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, a->ai_addr, a->ai_addrlen) != 0) { close(fd); fd = -1; }
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;
    /* Navtex only takes changes by POST and with its own header */
    snprintf(req, sizeof(req), "POST /api/settings/rx%d.enabled/%d HTTP/1.0\r\nHost: %s\r\nX-Navtex: wefax\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", rx, on ? 1 : 0, host);
    if (send(fd, req, strlen(req), MSG_NOSIGNAL) == (ssize_t) strlen(req)) {
        n = recv(fd, ans, sizeof(ans) - 1, 0);
        if (n > 12) { ans[n] = '\0'; if (strncmp(ans, "HTTP/1.", 7) == 0 && strncmp(ans + 9, "200", 3) == 0) ok = 0; }
    }
    close(fd);
    return ok;
}
