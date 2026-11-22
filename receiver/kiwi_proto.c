/*
 * Wefax receiver: the parts of the KiwiSDR protocol that need no network. See kiwi_proto.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "kiwi_proto.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- WebSocket */

static void base64(const unsigned char *in, size_t n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i < n; i += 3) {
        unsigned long v = (unsigned long) in[i] << 16 | (i + 1 < n ? (unsigned long) in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = T[v >> 18 & 63]; out[o++] = T[v >> 12 & 63];
        out[o++] = i + 1 < n ? T[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = '\0';
}

size_t kiwi_ws_request(char *out, size_t size, const char *host, int port, unsigned long stream_id, const unsigned char *key16)
{
    char key[32];
    int n;
    base64(key16, 16, key);
    n = snprintf(out, size,
                 "GET /%lu/SND HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nOrigin: http://%s:%d\r\n\r\n",
                 stream_id, host, port, key, host, port);
    return n > 0 && (size_t) n < size ? (size_t) n : 0;
}

int kiwi_ws_accepted(const char *a)
{
    return strncmp(a, "HTTP/1.1 101", 12) == 0 || strncmp(a, "HTTP/1.0 101", 12) == 0;
}

size_t kiwi_ws_frame(unsigned char *out, size_t size, int opcode, const void *payload, size_t len, const unsigned char *mask4)
{
    size_t h = 0, i;
    const unsigned char *p = payload;
    if (len > 65535 || size < len + 8) return 0;
    out[h++] = (unsigned char) (0x80 | (opcode & 0x0f));
    if (len < 126) out[h++] = (unsigned char) (0x80 | len);
    else { out[h++] = 0x80 | 126; out[h++] = (unsigned char) (len >> 8); out[h++] = (unsigned char) (len & 0xff); }
    memcpy(out + h, mask4, 4); h += 4;
    for (i = 0; i < len; i++) out[h + i] = p[i] ^ mask4[i & 3];
    return h + len;
}

int kiwi_ws_parse(const unsigned char *buf, size_t n, size_t max_payload, int *opcode, int *fin, const unsigned char **payload, size_t *payload_len, size_t *frame_len)
{
    size_t h = 2, len;
    if (n < 2) return 0;
    if (buf[0] & 0x70) return -1;                 /* extensions were not asked for */
    if (buf[1] & 0x80) return -1;                 /* a server does not mask */
    len = buf[1] & 0x7f;
    if (len == 126) {
        if (n < 4) return 0;
        len = (size_t) buf[2] << 8 | buf[3]; h = 4;
    } else if (len == 127) {
        int i;
        if (n < 10) return 0;
        if (buf[2] || buf[3] || buf[4] || buf[5]) return -1;
        for (len = 0, i = 6; i < 10; i++) len = len << 8 | buf[i];
        h = 10;
    }
    if (len > max_payload) return -1;
    if (n < h + len) return 0;
    *opcode = buf[0] & 0x0f; *fin = (buf[0] & 0x80) != 0;
    *payload = buf + h; *payload_len = len; *frame_len = h + len;
    return 1;
}

/* ---- messages */

int kiwi_msg_value(const char *body, size_t len, const char *name, char *out, size_t size)
{
    size_t i = 0, nl = strlen(name);
    while (i < len) {
        size_t s, e;
        while (i < len && body[i] == ' ') i++;
        s = i;
        while (i < len && body[i] != ' ') i++;
        e = i;
        if (e - s >= nl && memcmp(body + s, name, nl) == 0 && (e - s == nl || body[s + nl] == '=')) {
            size_t vl = e - s == nl ? 0 : e - s - nl - 1;
            if (size == 0) return 1;
            if (vl >= size) vl = size - 1;
            if (vl > 0) memcpy(out, body + s + nl + 1, vl);
            out[vl] = '\0';
            return 1;
        }
    }
    return 0;
}

static int hex(int c) { return isdigit(c) ? c - '0' : isxdigit(c) ? tolower(c) - 'a' + 10 : -1; }

int kiwi_cfg_position(const char *v, double *lat, double *lon)
{
    /* ..."rx_gps":"(36.530000, -6.290000)"... with %22 for the quotes and so on: undo that around the name */
    const char *p = v;
    while ((p = strstr(p, "rx_gps")) != NULL) {
        char t[96]; size_t o = 0; const char *q = p + 6;
        while (*q && o < sizeof(t) - 1) {
            if (*q == '%' && hex((unsigned char) q[1]) >= 0 && hex((unsigned char) q[2]) >= 0) { t[o++] = (char) (hex((unsigned char) q[1]) * 16 + hex((unsigned char) q[2])); q += 3; }
            else t[o++] = *q++;
        }
        t[o] = '\0';
        /* twice: the configuration holds the value URL-encoded inside the JSON, itself URL-encoded */
        { char u[96]; size_t k = 0, j = 0;
          while (t[j]) { if (t[j] == '%' && hex((unsigned char) t[j + 1]) >= 0 && hex((unsigned char) t[j + 2]) >= 0) { u[k++] = (char) (hex((unsigned char) t[j + 1]) * 16 + hex((unsigned char) t[j + 2])); j += 3; } else u[k++] = t[j++]; }
          u[k] = '\0'; memcpy(t, u, k + 1); }
        {
            const char *open = strchr(t, '(');
            double la, lo;
            if (open != NULL && open - t < 8 && sscanf(open + 1, "%lf , %lf", &la, &lo) == 2 && la >= -90 && la <= 90 && lo >= -180 && lo <= 180 && !(la == 0 && lo == 0)) {
                *lat = la; *lon = lo;
                return 1;
            }
        }
        p += 6;
    }
    return 0;
}

size_t kiwi_setup_commands(char *out, size_t size, double khz, double offset_khz)
{
    int n = snprintf(out, size,
                     "SET squelch=0 max=0\nSET genattn=0\nSET gen=0 mix=-1\nSET ident_user=Wefax\n"
                     "SET mod=iq low_cut=-5000 high_cut=5000 freq=%.3f\n"
                     "SET agc=1 hang=0 thresh=-100 slope=6 decay=1000 manGain=50\nSET compression=0\nSET keepalive\n",
                     khz - offset_khz);
    return n > 0 && (size_t) n < size ? (size_t) n : 0;
}

int kiwi_snd_iq(const unsigned char *b, size_t len, short *pairs, int max_pairs, double *rssi, int *flags)
{
    size_t i, n;
    if (len < 7) return -1;
    if (flags) *flags = b[0];
    if (rssi) *rssi = 0.1 * (b[5] << 8 | b[6]) - 127.0;
    if (!(b[0] & KIWI_SND_STEREO) || (b[0] & KIWI_SND_COMPRESSED)) return -1;
    if (len < 17) return 0;                       /* 10 bytes about the GPS time of the samples come first */
    n = (len - 17) / 4;
    if (n > (size_t) max_pairs) n = (size_t) max_pairs;
    b += 17;
    for (i = 0; i < 2 * n; i++) pairs[i] = (short) (b[2 * i] << 8 | b[2 * i + 1]);     /* big endian */
    return (int) n;
}

/* ---- resampler: windowed sinc between the samples, then the shift */

#define RS_HALF 8                 /* input samples each side */
#define RS_PHASES 512

struct kiwi_resampler {
    double step;                  /* input samples per output sample */
    double t;                     /* where the next output sample falls, in input samples after hi[RS_HALF - 1] */
    double hi[2 * RS_HALF], hq[2 * RS_HALF];
    float kernel[RS_PHASES + 1][2 * RS_HALF];
    double ph, dph;               /* the shift */
};

struct kiwi_resampler *kiwi_resampler_new(double in_rate, double out_rate, double shift_hz)
{
    struct kiwi_resampler *r;
    int p, k;
    if (in_rate < 1000 || out_rate < in_rate) return NULL;
    r = calloc(1, sizeof(*r));
    if (r == NULL) return NULL;
    r->step = in_rate / out_rate;
    r->dph = 2.0 * M_PI * shift_hz / out_rate;
    for (p = 0; p <= RS_PHASES; p++) {
        double frac = (double) p / RS_PHASES, sum = 0;
        for (k = 0; k < 2 * RS_HALF; k++) {
            /* tap k is the input sample at k - (RS_HALF - 1); the output is at frac */
            double x = (k - (RS_HALF - 1)) - frac;
            double w = 0.5 + 0.5 * cos(M_PI * x / RS_HALF);                      /* Hann over +-RS_HALF */
            double c = 0.92;                                                    /* keeps up to 0.46 of the input rate */
            double s = fabs(x) < 1e-9 ? c : sin(M_PI * c * x) / (M_PI * x);
            if (fabs(x) >= RS_HALF) w = 0;
            r->kernel[p][k] = (float) (s * w); sum += s * w;
        }
        for (k = 0; k < 2 * RS_HALF; k++) r->kernel[p][k] = (float) (r->kernel[p][k] / sum);
    }
    return r;
}

void kiwi_resampler_free(struct kiwi_resampler *r) { free(r); }

size_t kiwi_resample(struct kiwi_resampler *r, const short *in, size_t n, short *out, size_t max_out)
{
    size_t i, o = 0;
    for (i = 0; i < n; i++) {
        memmove(r->hi, r->hi + 1, sizeof(double) * (2 * RS_HALF - 1));
        memmove(r->hq, r->hq + 1, sizeof(double) * (2 * RS_HALF - 1));
        r->hi[2 * RS_HALF - 1] = in[2 * i]; r->hq[2 * RS_HALF - 1] = in[2 * i + 1];
        /* outputs that fall between hist[RS_HALF - 1] and hist[RS_HALF] */
        while (r->t < 1.0 && o < max_out) {
            const float *h = r->kernel[(int) (r->t * RS_PHASES + 0.5)];
            double si = 0, sq = 0, c = cos(r->ph), s = sin(r->ph), yi, yq;
            int k;
            for (k = 0; k < 2 * RS_HALF; k++) { si += h[k] * r->hi[k]; sq += h[k] * r->hq[k]; }
            yi = si * c - sq * s; yq = si * s + sq * c;
            out[2 * o] = (short) (yi > 32767 ? 32767 : yi < -32768 ? -32768 : lrint(yi));
            out[2 * o + 1] = (short) (yq > 32767 ? 32767 : yq < -32768 ? -32768 : lrint(yq));
            o++;
            r->t += r->step;
            r->ph += r->dph; if (r->ph > M_PI) r->ph -= 2.0 * M_PI;
        }
        r->t -= 1.0;                              /* the next input sample moves the window one on */
    }
    return o;
}
