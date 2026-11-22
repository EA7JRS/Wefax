/*
 * Wefax receiver: a KiwiSDR on the network as the receiver.
 *
 * Opens a WebSocket on the KiwiSDR, asks for the channel in "iq" mode and hands its samples
 * (about 12 kS/s) on as the other sources do: 252 kS/s with the channel 20 kHz above the centre.
 * No TLS: a KiwiSDR speaks plain ws:// on its own port (8073). The connection lasts while the
 * receiver is wanted on a frequency; a KiwiSDR that is public has a time limit for each visitor
 * and closes it itself.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "iq_source.h"
#include "kiwi_proto.h"

#define MAX_FRAME (4u << 20)          /* the configuration a KiwiSDR sends at the start is some hundred kB */
#define CONNECT_TIMEOUT_S 10

double kiwi_lat = 0, kiwi_lon = 0;    /* where the KiwiSDR says it is */
volatile sig_atomic_t kiwi_has_position = 0;
double kiwi_rssi_dbm = -127;

static int sock = -1;
static double tuned_khz;

static int send_all(const void *p, size_t n)
{
    const char *c = p;
    while (n > 0) {
        ssize_t w = send(sock, c, n, MSG_NOSIGNAL);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return -1; }
        c += w; n -= (size_t) w;
    }
    return 0;
}

static int send_text(const char *text)
{
    unsigned char f[600], mask[4];
    size_t n;
    int i;
    for (i = 0; i < 4; i++) mask[i] = (unsigned char) (rand() & 0xff);
    n = kiwi_ws_frame(f, sizeof(f), 1, text, strlen(text), mask);
    return n == 0 ? -1 : send_all(f, n);
}

static int send_lines(char *lines)
{
    char *l = lines, *e;
    while ((e = strchr(l, '\n')) != NULL) { *e = '\0'; if (send_text(l) != 0) return -1; l = e + 1; }
    return 0;
}

static int connect_to(const char *host, int port)
{
    struct addrinfo hints, *res, *a;
    char service[12];
    int s = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo(host, service, &hints, &res) != 0) return -1;
    for (a = res; a != NULL; a = a->ai_next) {
        struct timeval tv = { CONNECT_TIMEOUT_S, 0 };
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s < 0) continue;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(s, a->ai_addr, a->ai_addrlen) == 0) break;
        close(s); s = -1;
    }
    freeaddrinfo(res);
    return s;
}

/* What a KiwiSDR says when it will not serve; NULL when the message is not a refusal */
static const char *refusal(const char *body, size_t len)
{
    char v[16];
    if (kiwi_msg_value(body, len, "too_busy", v, sizeof(v))) return "all its channels are taken";
    if (kiwi_msg_value(body, len, "badp", v, sizeof(v)) && v[0] != '0')
        return v[0] == '1' ? "wrong password, or all the channels that need none are taken" :
               v[0] == '5' ? "it does not allow two connections from the same address" : "it does not let this client in";
    if (kiwi_msg_value(body, len, "down", v, sizeof(v))) return "it is down for now";
    if (kiwi_msg_value(body, len, "redirect", v, sizeof(v))) return "it sends its visitors to another receiver";
    return NULL;
}

static void *kiwi_reader(void *arg)
{
    unsigned char *buf = malloc(65536), *whole = NULL;
    size_t cap = 65536, have = 0, whole_n = 0;
    struct kiwi_resampler *rs = NULL;
    double offset_khz = 0;
    time_t last_alive = 0;
    static short pairs[8192], up[2 * 8192 * 24], xi[8192 * 24], xq[8192 * 24];
    int said_overload = 0;
    (void) arg;

    while (buf != NULL && !stop_requested && !retune_requested && !device_lost) {
        struct pollfd p = { sock, POLLIN, 0 };
        int opcode, fin, r;
        const unsigned char *pl; size_t pln, fln;

        if (time(NULL) != last_alive) { last_alive = time(NULL); if (send_text("SET keepalive") != 0) break; }
        r = poll(&p, 1, 500);
        if (r < 0 && errno != EINTR) break;
        if (r > 0) {
            ssize_t got;
            if (have == cap) {
                unsigned char *b2;
                if (cap >= MAX_FRAME + 16) { printf("kiwi: a message too large\n"); break; }
                b2 = realloc(buf, cap * 2); if (b2 == NULL) break;
                buf = b2; cap *= 2;
            }
            got = recv(sock, buf + have, cap - have, 0);
            if (got == 0) { if (!stop_requested && !retune_requested) printf("kiwi: the KiwiSDR closed the connection (a public one limits the time of each visitor)\n"); break; }
            if (got < 0) { if (errno == EINTR || errno == EAGAIN) continue; if (!stop_requested && !retune_requested) printf("kiwi: connection lost\n"); break; }
            have += (size_t) got;
        }
        while ((r = kiwi_ws_parse(buf, have, MAX_FRAME, &opcode, &fin, &pl, &pln, &fln)) == 1) {
            const unsigned char *msg = pl; size_t mn = pln;
            if (opcode == 8) { printf("kiwi: the KiwiSDR closed the connection (a public one limits the time of each visitor)\n"); goto out; }
            if (opcode == 9) {                                         /* ping: answer it */
                unsigned char f[140], mask[4] = { 1, 2, 3, 4 };
                size_t n = kiwi_ws_frame(f, sizeof(f), 10, pl, pln > 125 ? 125 : pln, mask);
                if (n) send_all(f, n);
            } else if (opcode <= 2) {
                /* a message in pieces: put them together */
                if (!fin || opcode == 0) {
                    unsigned char *w2 = realloc(whole, whole_n + pln + 1);
                    if (w2 == NULL) goto out;
                    whole = w2; memcpy(whole + whole_n, pl, pln); whole_n += pln;
                    msg = whole; mn = whole_n;
                }
                if (fin && mn >= 3) {
                    if (memcmp(msg, "MSG", 3) == 0) {
                        const char *body = (const char *) msg + 3; size_t bl = mn - 3;
                        char v[40];
                        const char *why = refusal(body, bl);
                        if (why != NULL) { printf("kiwi: the KiwiSDR refuses: %s\n", why); goto out; }
                        if (kiwi_msg_value(body, bl, "freq_offset", v, sizeof(v))) offset_khz = atof(v);
                        if (kiwi_msg_value(body, bl, "audio_rate", v, sizeof(v))) {
                            char c[64]; snprintf(c, sizeof(c), "SET AR OK in=%d out=44100", atoi(v));
                            if (send_text(c) != 0) goto out;
                        }
                        if (kiwi_msg_value(body, bl, "sample_rate", v, sizeof(v)) && rs == NULL) {
                            double rate = atof(v);
                            char c[400];
                            if (rate < 8000 || rate > 48000) { printf("kiwi: a sample rate that makes no sense (%s)\n", v); goto out; }
                            rs = kiwi_resampler_new(rate, IQ_SAMPLE_RATE, IQ_TUNE_BELOW_HZ);
                            if (rs == NULL || kiwi_setup_commands(c, sizeof(c), tuned_khz, offset_khz) == 0 || send_lines(c) != 0) goto out;
                            printf("kiwi: %.3f kHz, %.3f samples a second\n", tuned_khz, rate);
                        }
                        if (bl > 9 && !kiwi_has_position) {
                            /* the configuration: too long for kiwi_msg_value's buffers, so it is searched where it lies */
                            char *z = malloc(bl + 1);
                            if (z != NULL) {
                                memcpy(z, body, bl); z[bl] = '\0';
                                if (strstr(z, "load_cfg=") != NULL && kiwi_cfg_position(strstr(z, "load_cfg="), &kiwi_lat, &kiwi_lon)) {
                                    kiwi_has_position = 1;
                                    printf("kiwi: the receiver says it is at %.3f, %.3f\n", kiwi_lat, kiwi_lon);
                                }
                                free(z);
                            }
                        }
                    } else if (memcmp(msg, "SND", 3) == 0 && rs != NULL) {
                        int flags = 0, n = kiwi_snd_iq(msg + 3, mn - 3, pairs, 8192, &kiwi_rssi_dbm, &flags);
                        if (n < 0) { printf("kiwi: the KiwiSDR is not sending I/Q samples\n"); goto out; }
                        if ((flags & KIWI_SND_ADC_OVERLOAD) && !said_overload) { said_overload = 1; printf("kiwi: the KiwiSDR reports its input overloaded\n"); }
                        if (n > 0) {
                            size_t m = kiwi_resample(rs, pairs, (size_t) n, up, 8192 * 24), i;
                            for (i = 0; i < m; i++) { xi[i] = up[2 * i]; xq[i] = up[2 * i + 1]; }
                            iq_push_short(xi, xq, (unsigned int) m);
                        }
                    }
                }
                if (fin) { free(whole); whole = NULL; whole_n = 0; }
            }
            memmove(buf, buf + fln, have - fln); have -= fln;
        }
        if (r < 0) { printf("kiwi: what the KiwiSDR sends is not understood\n"); break; }
    }
out:
    if (!stop_requested && !retune_requested) device_lost = 1;
    kiwi_resampler_free(rs);
    free(whole); free(buf);
    return NULL;
}

int kiwi_source_run(void)
{
    pthread_t reader;
    char req[512], ans[1024], auth[120];
    unsigned char key[16];
    size_t n, got = 0;
    int i, failed = 1;
    const char *host = iq_opt.kiwi_host;
    int port = iq_opt.kiwi_port > 0 ? iq_opt.kiwi_port : 8073;

    if (host == NULL || host[0] == '\0') { printf("kiwi: no KiwiSDR given (its name or address)\n"); return 1; }
    tuned_khz = (iq_tuned_hz + IQ_TUNE_BELOW_HZ) / 1000.0;
    if (tuned_khz < 0 || tuned_khz > 32000) { printf("kiwi: a KiwiSDR covers 0 to 30 MHz (32 some)\n"); return 1; }
    sock = connect_to(host, port);
    if (sock < 0) { printf("kiwi: cannot connect to %s:%d\n", host, port); return 1; }

    srand((unsigned) (time(NULL) ^ getpid()));
    for (i = 0; i < 16; i++) key[i] = (unsigned char) (rand() & 0xff);
    n = kiwi_ws_request(req, sizeof(req), host, port, (unsigned long) (time(NULL) + getpid()) & 0xffffffffUL, key);
    if (n == 0 || send_all(req, n) != 0) { printf("kiwi: cannot talk to %s:%d\n", host, port); goto close_sock; }
    while (got < sizeof(ans) - 1) {                                   /* the answer, up to its empty line, byte by byte */
        ssize_t r = recv(sock, ans + got, 1, 0);
        if (r <= 0) break;
        got++; ans[got] = '\0';
        if (got >= 4 && memcmp(ans + got - 4, "\r\n\r\n", 4) == 0) break;
    }
    if (!kiwi_ws_accepted(ans)) {
        ans[strcspn(ans, "\r\n")] = '\0';
        printf("kiwi: %s:%d does not answer as a KiwiSDR (%s)\n", host, port, got ? ans : "nothing");
        goto close_sock;
    }
    snprintf(auth, sizeof(auth), "SET auth t=kiwi p=%s", iq_opt.kiwi_password != NULL && iq_opt.kiwi_password[0] ? iq_opt.kiwi_password : "#");
    if (send_text(auth) != 0) goto close_sock;
    printf("kiwi: connected to %s:%d\n", host, port);

    if (iq_ring_init() != 0) { printf("out of memory\n"); goto close_sock; }
    if (pthread_create(&reader, NULL, kiwi_reader, NULL) != 0) { printf("kiwi: cannot start the reader thread\n"); iq_ring_free(); goto close_sock; }
    consume_loop();
    failed = device_lost ? 1 : 0;
    if (stop_requested) printf("Stop requested - leaving the KiwiSDR\n");
    shutdown(sock, SHUT_RDWR);
    pthread_join(reader, NULL);
    iq_ring_free();

close_sock:
    close(sock); sock = -1;
    return failed;
}
