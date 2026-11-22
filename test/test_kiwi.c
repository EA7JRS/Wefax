/*
 * Wefax · test of the KiwiSDR source without a KiwiSDR: the WebSocket frames, the messages, and
 * a broadcast made here (never transmitted) packed as a KiwiSDR sends its samples, taken through
 * the resampler, the channel and the decoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../receiver/channel.h"
#include "../receiver/kiwi_proto.h"
#include "../receiver/wefax_decoder.h"

static int checks, failed;
#define CHECK(c, what) do { checks++; if (!(c)) { failed++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

static double pattern(int x, int y, int width)
{
    if (y < 20) return (x / 100) % 2 ? 1.0 : 0.0;
    if (y < 40) return (double) x / width;
    if (abs(x - y * 4) < 6) return 0.0;
    return 1.0;
}

static double broadcast_hz(double t, int lines)
{
    const double line = 0.5; const int width = 1810;
    if (t < 1.0) return 0;
    t -= 1.0;
    if (t < 5.0) return fmod(t * 300.0, 1.0) < 0.5 ? 400.0 : -400.0;
    t -= 5.0;
    if (t < 30.0) { double p = fmod(t / line, 1.0); return p < 0.025 || p >= 0.975 ? 400.0 : -400.0; }
    t -= 30.0;
    if (t < lines * line) { double l = t / line; int y = (int) l; return pattern((int) ((l - y) * width), y, width) * 800.0 - 400.0; }
    t -= lines * line;
    if (t < 5.0) return fmod(t * 450.0, 1.0) < 0.5 ? 400.0 : -400.0;
    return -400.0;
}

struct chart { int begun, ended, lines, drop, width, phased; double ppm; unsigned char *px; int cap; };
static void on_begin(void *u, int ioc, int lpm, int width, double ppm, int phased) { struct chart *c = u; (void) ioc; (void) lpm; c->begun++; c->width = width; c->ppm = ppm; c->phased = phased; }
static void on_line(void *u, const unsigned char *p, int width)
{
    struct chart *c = u;
    if (c->lines == c->cap) { c->cap = c->cap ? c->cap * 2 : 256; c->px = realloc(c->px, (size_t) c->cap * WEFAX_MAX_WIDTH); }
    memcpy(c->px + (size_t) c->lines * WEFAX_MAX_WIDTH, p, (size_t) width); c->lines++;
}
static void on_end(void *u, enum wefax_end why, int drop) { struct chart *c = u; (void) why; c->ended++; c->drop = drop; }

static double error_of(const struct chart *c, int lines, int *best_shift)
{
    double best = 1e9; int sh;
    for (sh = -8; sh <= 8; sh++) {
        double sum = 0; long n = 0; int y, x;
        for (y = 0; y < lines && y < c->lines; y++)
            for (x = 10; x < c->width - 10; x++) { sum += fabs(c->px[(size_t) y * WEFAX_MAX_WIDTH + x + sh] - 255.0 * pattern(x, y, c->width)); n++; }
        if (n > 0 && sum / n < best) { best = sum / n; if (best_shift) *best_shift = sh; }
    }
    return best;
}

/* A broadcast as a KiwiSDR would send it: SND messages of 512 pairs at `rate` samples a second,
   big endian, after the 7 bytes of the message and the 10 of the GPS time. said_rate: the rate
   the receiver is told (the KiwiSDR says its own). */
static struct chart through_kiwi(int lines, double rate, double said_rate)
{
    struct chart c; struct wefax_events ev = { &c, on_begin, on_line, on_end };
    struct wefax_decoder *d = wefax_new(CHANNEL_OUT_RATE, 120, &ev);
    struct channel *ch = channel_new(20000.0);
    struct kiwi_resampler *rs = kiwi_resampler_new(said_rate, CHANNEL_IN_RATE, 20000.0);
    double total = 1.0 + 5.0 + 30.0 + lines * 0.5 + 5.0 + 8.0, phase = 0;
    long n = (long) (total * rate), i; int k = 0;
    unsigned char body[17 + 4 * 512];
    static short pairs[512], up[2 * 512 * 24]; static float hz[512 * 24 / 21 + 8];
    memset(&c, 0, sizeof(c));
    memset(body, 0, sizeof(body));
    body[0] = KIWI_SND_STEREO; body[5] = 0x02; body[6] = 0x9e;       /* 670 tenths: -60 dBm */
    for (i = 0; i < n; i++) {
        short si, sq;
        phase += 2.0 * M_PI * broadcast_hz((double) i / rate, lines) / rate;
        if (phase > M_PI) phase -= 2.0 * M_PI;
        si = (short) (8000.0 * cos(phase)); sq = (short) (8000.0 * sin(phase));
        body[17 + 4 * k] = (unsigned char) ((unsigned short) si >> 8); body[17 + 4 * k + 1] = (unsigned char) (si & 0xff);
        body[17 + 4 * k + 2] = (unsigned char) ((unsigned short) sq >> 8); body[17 + 4 * k + 3] = (unsigned char) (sq & 0xff);
        if (++k == 512) {
            int got = kiwi_snd_iq(body, sizeof(body), pairs, 512, NULL, NULL);
            size_t m = kiwi_resample(rs, pairs, (size_t) got, up, 512 * 24);
            size_t v = channel_feed(ch, up, m, hz, NULL);
            wefax_feed(d, hz, v);
            k = 0;
        }
    }
    kiwi_resampler_free(rs); channel_free(ch); wefax_free(d);
    return c;
}

int main(void)
{
    /* ---- WebSocket */
    {
        unsigned char key[16], f[300], mask[4] = { 0x11, 0x22, 0x33, 0x44 };
        char req[400]; size_t n; int i, op, fin; const unsigned char *pl; size_t pln, fln;
        for (i = 0; i < 16; i++) key[i] = (unsigned char) i;
        n = kiwi_ws_request(req, sizeof(req), "kiwi.example", 8073, 1234567UL, key);
        CHECK(n > 0 && strncmp(req, "GET /1234567/SND HTTP/1.1\r\nHost: kiwi.example:8073\r\n", 52) == 0, "the request asks for the sound stream");
        CHECK(strstr(req, "Sec-WebSocket-Key: AAECAwQFBgcICQoLDA0ODw==\r\n") != NULL && strstr(req, "Sec-WebSocket-Version: 13\r\n") != NULL, "with its key in base64");
        CHECK(n >= 4 && memcmp(req + n - 4, "\r\n\r\n", 4) == 0 && kiwi_ws_request(req, 40, "kiwi.example", 8073, 1, key) == 0, "ends in an empty line; does not overflow");
        CHECK(kiwi_ws_accepted("HTTP/1.1 101 Switching Protocols\r\n") && !kiwi_ws_accepted("HTTP/1.1 200 OK\r\n") && !kiwi_ws_accepted(""), "101 is yes, anything else is no");

        n = kiwi_ws_frame(f, sizeof(f), 1, "SET keepalive", 13, mask);
        CHECK(n == 19 && f[0] == 0x81 && f[1] == (0x80 | 13) && memcmp(f + 2, mask, 4) == 0 && (f[6] ^ 0x11) == 'S' && (f[7] ^ 0x22) == 'E', "a short text frame, masked");
        {
            char big[200]; memset(big, 'x', sizeof(big));
            n = kiwi_ws_frame(f, sizeof(f), 1, big, 200, mask);
            CHECK(n == 208 && f[1] == (0x80 | 126) && f[2] == 0 && f[3] == 200, "a frame of 200 bytes says its length in two bytes");
            CHECK(kiwi_ws_frame(f, 100, 1, big, 200, mask) == 0, "a frame that does not fit is not written");
        }
        /* from the server: not masked */
        { const unsigned char s[] = { 0x82, 0x05, 'M', 'S', 'G', ' ', 'a', 0x82 };
          CHECK(kiwi_ws_parse(s, 1, 100, &op, &fin, &pl, &pln, &fln) == 0 && kiwi_ws_parse(s, 6, 100, &op, &fin, &pl, &pln, &fln) == 0, "half a frame: wait for more");
          CHECK(kiwi_ws_parse(s, 8, 100, &op, &fin, &pl, &pln, &fln) == 1 && op == 2 && fin == 1 && pln == 5 && fln == 7 && memcmp(pl, "MSG a", 5) == 0, "a whole frame, with what follows left alone"); }
        { unsigned char s[4 + 300]; s[0] = 0x02; s[1] = 126; s[2] = 0x01; s[3] = 0x2c; memset(s + 4, 7, 300);
          CHECK(kiwi_ws_parse(s, sizeof(s), 1000, &op, &fin, &pl, &pln, &fln) == 1 && fin == 0 && pln == 300 && fln == 304, "300 bytes, first piece of a message");
          CHECK(kiwi_ws_parse(s, sizeof(s), 200, &op, &fin, &pl, &pln, &fln) == -1, "larger than allowed"); }
        { const unsigned char s[] = { 0x82, 127, 0, 0, 0, 0, 0, 1, 0, 0 };
          CHECK(kiwi_ws_parse(s, sizeof(s), 1u << 20, &op, &fin, &pl, &pln, &fln) == 0, "65536 bytes announced, not here yet"); }
        { const unsigned char s[] = { 0x81, 0x85, 1, 2, 3, 4, 0, 0, 0, 0, 0 };
          CHECK(kiwi_ws_parse(s, sizeof(s), 100, &op, &fin, &pl, &pln, &fln) == -1, "a masked frame does not come from a server"); }
        { const unsigned char s[] = { 0x88, 0x00 };
          CHECK(kiwi_ws_parse(s, 2, 100, &op, &fin, &pl, &pln, &fln) == 1 && op == 8 && pln == 0, "close"); }
    }

    /* ---- messages */
    {
        const char *m = " audio_init=0 audio_rate=12000 sample_rate=12001.135 too_busy version_maj=1";
        char v[32]; double la = 0, lo = 0; char c[400];
        CHECK(kiwi_msg_value(m, strlen(m), "sample_rate", v, sizeof(v)) && strcmp(v, "12001.135") == 0, "the exact sample rate");
        CHECK(kiwi_msg_value(m, strlen(m), "audio_rate", v, sizeof(v)) && strcmp(v, "12000") == 0, "the nominal rate");
        CHECK(kiwi_msg_value(m, strlen(m), "too_busy", v, sizeof(v)) && v[0] == '\0', "a name with no value");
        CHECK(!kiwi_msg_value(m, strlen(m), "rate", v, sizeof(v)) && !kiwi_msg_value(m, strlen(m), "audio", v, sizeof(v)) && !kiwi_msg_value(m, strlen(m), "badp", v, sizeof(v)), "part of a name is not the name");
        CHECK(kiwi_msg_value(m, strlen(m), "sample_rate", v, 6) && strcmp(v, "12001") == 0, "a short buffer is not overrun");
        /* the position inside the configuration: JSON, URL-encoded, with the value encoded again */
        CHECK(kiwi_cfg_position("load_cfg=%7B%22index_html_params%22%3A%7B%7D%2C%22rx_gps%22%3A%22(36.530000%252C%2520-6.290000)%22%2C%22rx_asl%22%3A20%7D", &la, &lo) && fabs(la - 36.53) < 1e-6 && fabs(lo + 6.29) < 1e-6, "the receiver's position, encoded twice");
        CHECK(kiwi_cfg_position("load_cfg=%7B%22rx_gps%22%3A%22(-41.2%2C%20174.8)%22%7D", &la, &lo) && fabs(la + 41.2) < 1e-6 && fabs(lo - 174.8) < 1e-6, "encoded once, southern hemisphere");
        CHECK(!kiwi_cfg_position("load_cfg=%7B%22rx_gps%22%3A%22(0.000000%2C%200.000000)%22%7D", &la, &lo), "0, 0 is a receiver that was never told where it is");
        CHECK(!kiwi_cfg_position("load_cfg=%7B%22rx_name%22%3A%22x%22%7D", &la, &lo) && !kiwi_cfg_position("load_cfg=%7B%22rx_gps%22%3A%22(95%2C%200)%22%7D", &la, &lo), "no position, or an impossible one");
        CHECK(kiwi_setup_commands(c, sizeof(c), 7880.0, 0) > 0 && strstr(c, "SET mod=iq low_cut=-5000 high_cut=5000 freq=7880.000\n") != NULL && strstr(c, "SET compression=0\n") != NULL, "asks for I/Q on the channel, not compressed");
        CHECK(kiwi_setup_commands(c, sizeof(c), 128030.0, 116000.0) > 0 && strstr(c, "freq=12030.000\n") != NULL, "a receiver behind a converter: its own offset is taken off");
        CHECK(kiwi_setup_commands(c, 50, 7880.0, 0) == 0, "does not overflow");
    }

    /* ---- samples */
    {
        unsigned char b[17 + 8]; short p[8]; double rssi = 0; int flags = 0;
        memset(b, 0, sizeof(b));
        b[0] = KIWI_SND_STEREO | KIWI_SND_ADC_OVERLOAD; b[5] = 0x02; b[6] = 0x9e;
        b[17] = 0x12; b[18] = 0x34; b[19] = 0xff; b[20] = 0xfe; b[21] = 0x80; b[22] = 0x00; b[23] = 0x7f; b[24] = 0xff;
        CHECK(kiwi_snd_iq(b, sizeof(b), p, 8, &rssi, &flags) == 2 && p[0] == 0x1234 && p[1] == -2 && p[2] == -32768 && p[3] == 32767, "two pairs, big endian, after the 17 bytes of header");
        CHECK(fabs(rssi + 60.0) < 1e-9 && (flags & KIWI_SND_ADC_OVERLOAD), "the level the receiver measures, and its overload flag");
        CHECK(kiwi_snd_iq(b, sizeof(b), p, 1, NULL, NULL) == 1 && kiwi_snd_iq(b, 17, p, 8, NULL, NULL) == 0, "no more than fit; a message with no samples");
        b[0] = 0; CHECK(kiwi_snd_iq(b, sizeof(b), p, 8, NULL, NULL) == -1, "mono sound is not I/Q");
        b[0] = KIWI_SND_STEREO | KIWI_SND_COMPRESSED; CHECK(kiwi_snd_iq(b, sizeof(b), p, 8, NULL, NULL) == -1, "nor compressed sound");
        CHECK(kiwi_snd_iq(b, 5, p, 8, NULL, NULL) == -1, "a cut message");
    }

    /* ---- resampler: a tone 1 kHz above the centre comes out at 21 kHz, at 252 kS/s */
    {
        struct kiwi_resampler *rs = kiwi_resampler_new(12000.0, 252000.0, 20000.0);
        static short in[2 * 1200], out[2 * 1200 * 22];
        size_t m, total = 0, i; double ph = 0, re = 0, im = 0, pw = 0, img = 0, imre = 0, imim = 0; int k;
        CHECK(rs != NULL && kiwi_resampler_new(500.0, 252000.0, 0) == NULL, "a rate that makes no sense gives no resampler");
        for (k = 0; k < 10; k++) {
            for (i = 0; i < 1200; i++) { in[2 * i] = (short) (10000 * cos(ph)); in[2 * i + 1] = (short) (10000 * sin(ph)); ph += 2 * M_PI * 1000.0 / 12000.0; }
            m = kiwi_resample(rs, in, 1200, out, 1200 * 22);
            total += m;
            if (k == 9) for (i = 0; i < m; i++) {
                double t = (double) i / 252000.0, a = 2 * M_PI * 21000.0 * t, b = 2 * M_PI * 33000.0 * t;
                re += out[2 * i] * cos(a) + out[2 * i + 1] * sin(a); im += out[2 * i + 1] * cos(a) - out[2 * i] * sin(a);
                imre += out[2 * i] * cos(b) + out[2 * i + 1] * sin(b); imim += out[2 * i + 1] * cos(b) - out[2 * i] * sin(b);
                pw += (double) out[2 * i] * out[2 * i] + (double) out[2 * i + 1] * out[2 * i + 1];
            }
        }
        CHECK(labs((long) total - 252000) <= 2, "one second in, one second out");
        pw /= m; re /= m; im /= m; img = (imre * imre + imim * imim) / ((double) m * m);
        printf("resampler: tone %.0f of 10000, power elsewhere %.4f %%, image at +12 kHz %.1f dB\n", sqrt(re * re + im * im), 100.0 * (pw - re * re - im * im) / pw, 10 * log10(img / (re * re + im * im) + 1e-30));
        CHECK(sqrt(re * re + im * im) > 9500 && sqrt(re * re + im * im) < 10500, "the tone keeps its size, 20 kHz up");
        CHECK((pw - re * re - im * im) / pw < 0.002, "and nearly nothing else comes out");
        CHECK(10 * log10(img / (re * re + im * im) + 1e-30) < -50, "its image 12 kHz away is 50 dB down");
        kiwi_resampler_free(rs);
    }

    /* ---- a whole broadcast through it */
    {
        int shift = 0; double e;
        struct chart c = through_kiwi(100, 12000.0, 12000.0);
        e = error_of(&c, 80, &shift);
        printf("kiwi 12000: begun %d ended %d lines %d error %.1f shift %d clock %.1f ppm\n", c.begun, c.ended, c.lines, e, shift, c.ppm);
        CHECK(c.begun == 1 && c.ended == 1 && c.phased == 1 && c.width == 1810, "a chart begins and ends, phased");
        CHECK(e < 8.0 && abs(shift) <= 2, "the chart is the one sent");
        CHECK(fabs(c.ppm) < 8.0, "no clock error where there is none");
        free(c.px);
        /* a KiwiSDR whose rate is not 12000: it says so, and the chart is as good */
        c = through_kiwi(100, 12001.135, 12001.135);
        e = error_of(&c, 80, &shift);
        printf("kiwi 12001.135: lines %d error %.1f shift %d clock %.1f ppm\n", c.lines, e, shift, c.ppm);
        CHECK(c.begun == 1 && e < 8.0 && abs(shift) <= 2 && fabs(c.ppm) < 8.0, "the rate the KiwiSDR says is used");
        free(c.px);
        /* and one that says 12000 but is 95 parts per million off: the phasing measures it */
        c = through_kiwi(100, 12001.135, 12000.0);
        e = error_of(&c, 80, &shift);
        printf("kiwi wrong rate: error %.1f shift %d clock %.1f ppm\n", e, shift, c.ppm);
        CHECK(c.begun == 1 && e < 8.0 && fabs(fabs(c.ppm) - 94.6) < 12.0, "a rate said wrong is a clock error the decoder corrects");
        free(c.px);
    }

    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
