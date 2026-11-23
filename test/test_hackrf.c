/*
 * Wefax · test of the HackRF source without a HackRF: the filter that takes its 2.016 MS/s of
 * 8 bit samples down to 252 kS/s, and a broadcast made here (never transmitted) as a HackRF
 * would deliver it, with noise and a strong station that must not fold onto the channel, taken
 * through that filter, the channel and the decoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../receiver/channel.h"
#include "../receiver/decim.h"
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

/* Level in dB (0 = full scale) of a tone at `hz` after the filter, measured at the frequency it lands on */
static double tone_after(double hz)
{
    struct decim *d = decim_new();
    static signed char in[2 * 8 * 4096]; static short out[2 * 4097];
    double landed = fmod(hz + 126000.0 + 252000.0 * 100, 252000.0) - 126000.0, re = 0, im = 0; size_t m, i;
    for (i = 0; i < 8 * 4096; i++) { double p = 2.0 * M_PI * hz * (double) i / DECIM_IN_RATE; in[2 * i] = (signed char) lrint(100.0 * cos(p)); in[2 * i + 1] = (signed char) lrint(100.0 * sin(p)); }
    m = decim_feed(d, in, 8 * 4096, out);
    for (i = 64; i < m; i++) { double p = -2.0 * M_PI * landed * (double) i / 252000.0; re += out[2 * i] * cos(p) - out[2 * i + 1] * sin(p); im += out[2 * i] * sin(p) + out[2 * i + 1] * cos(p); }
    decim_free(d);
    return 20.0 * log10(sqrt(re * re + im * im) / (double) (m - 64) / 25600.0 + 1e-12);
}

static unsigned long seed = 12345;
static double noise(void) { double s = 0; int k; for (k = 0; k < 4; k++) { seed = seed * 1103515245UL + 12345UL; s += (double) ((seed >> 16) & 0x7fff) / 32768.0; } return (s - 2.0) * 1.73; }

/* A broadcast as a HackRF would deliver it: 2.016 MS/s, 8 bit, the channel 20 kHz above the centre */
static struct chart through_hackrf(int lines, double signal, double noise_rms, double other)
{
    struct chart c; struct wefax_events ev = { &c, on_begin, on_line, on_end };
    struct wefax_decoder *d = wefax_new(CHANNEL_OUT_RATE, 120, &ev);
    struct channel *ch = channel_new(20000.0);
    struct decim *dc = decim_new();
    const double rate = DECIM_IN_RATE;
    double total = 1.0 + 5.0 + 30.0 + lines * 0.5 + 5.0 + 8.0, phase = 0, po = 0;
    long n = (long) (total * rate), i; int k = 0;
    static signed char in[2 * 16384]; static short mid[2 * 2049]; static float hz[2049 / 21 + 8];
    memset(&c, 0, sizeof(c));
    for (i = 0; i < n; i++) {
        double vi, vq;
        phase += 2.0 * M_PI * (20000.0 + broadcast_hz((double) i / rate, lines)) / rate;
        if (phase > M_PI) phase -= 2.0 * M_PI;
        po += 2.0 * M_PI * 272000.0 / rate;                 /* 252 kHz above the channel: it would fold right onto it */
        if (po > M_PI) po -= 2.0 * M_PI;
        vi = signal * cos(phase) + other * cos(po) + noise_rms * noise();
        vq = signal * sin(phase) + other * sin(po) + noise_rms * noise();
        in[2 * k] = (signed char) (vi > 127 ? 127 : vi < -127 ? -127 : lrint(vi));
        in[2 * k + 1] = (signed char) (vq > 127 ? 127 : vq < -127 ? -127 : lrint(vq));
        if (++k == 16384) {
            size_t m = decim_feed(dc, in, 16384, mid);
            size_t v = channel_feed(ch, mid, m, hz, NULL);
            wefax_feed(d, hz, v);
            k = 0;
        }
    }
    decim_free(dc); channel_free(ch); wefax_free(d);
    return c;
}

int main(void)
{
    /* ---- the filter: what it keeps and what it takes off */
    {
        double pass0 = tone_after(0.0), pass20 = tone_after(20000.0), pass50 = tone_after(-50000.0);
        double fold = tone_after(272000.0), fold2 = tone_after(-232000.0), fold3 = tone_after(504000.0 + 20000.0), far = tone_after(1008000.0 + 20000.0);
        printf("filter: 0 Hz %.2f dB, +20 kHz %.2f dB, -50 kHz %.2f dB; folding: 272 kHz %.1f dB, -232 kHz %.1f dB, 524 kHz %.1f dB, 1028 kHz %.1f dB\n", pass0, pass20, pass50, fold, fold2, fold3, far);
        CHECK(fabs(pass0) < 0.1 && fabs(pass20) < 0.1, "the channel passes untouched, the 8 bits brought to 16");
        CHECK(fabs(pass50) < 0.5, "and 50 kHz to each side");
        CHECK(fold < -70.0 && fold2 < -70.0 && fold3 < -70.0 && far < -70.0, "what would fold onto the channel is taken off by more than 70 dB");
    }
    {
        struct decim *d = decim_new(); static signed char in[2 * 100]; static short out[2 * 14]; size_t a, b;
        memset(in, 0, sizeof(in));
        a = decim_feed(d, in, 13, out); b = decim_feed(d, in, 87, out);
        CHECK(a == 1 && b == 11, "one sample out of every 8, whatever the size of the pieces");
        decim_free(d);
    }

    /* ---- a whole broadcast through it */
    {
        int shift = 0; double e;
        struct chart c = through_hackrf(60, 40.0, 0.0, 0.0);
        e = error_of(&c, 50, &shift);
        printf("hackrf clean: begun %d ended %d lines %d error %.1f shift %d clock %.1f ppm\n", c.begun, c.ended, c.lines, e, shift, c.ppm);
        CHECK(c.begun == 1 && c.ended == 1 && c.phased == 1 && c.width == 1810, "a chart begins and ends, phased");
        CHECK(e < 8.0 && abs(shift) <= 2 && fabs(c.ppm) < 8.0, "the chart is the one sent");
        free(c.px);
        /* a weak signal (16 of 127), noise stronger than it across the 2 MHz and a station 8 dB stronger where it would fold */
        c = through_hackrf(60, 16.0, 20.0, 40.0);
        e = error_of(&c, 50, &shift);
        printf("hackrf weak, noise and a strong station: begun %d lines %d error %.1f shift %d\n", c.begun, c.lines, e, shift);
        CHECK(c.begun == 1 && c.phased == 1 && e < 20.0 && abs(shift) <= 2, "a signal well under the noise of the whole 2 MHz is still received");
        free(c.px);
    }

    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
