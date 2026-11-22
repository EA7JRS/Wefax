/*
 * Wefax · test of the whole chain: receiver samples -> channel -> decoder -> PNG,
 * with a broadcast made here (never transmitted), noise and a receiver off frequency.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../receiver/channel.h"
#include "../receiver/png_out.h"
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

/* Frequency of the broadcast at time t (seconds), relative to the centre of its channel */
static double broadcast_hz(double t, int lines)
{
    const double line = 0.5; const int width = 1810;
    if (t < 1.0) return 0;                                            /* carrier before it starts */
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

static double gauss(void)
{
    double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = (rand() + 1.0) / (RAND_MAX + 2.0);
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

struct chart { int begun, ended, lines, drop, width; enum wefax_end why; double ppm; unsigned char *px; int cap; };
static void on_begin(void *u, int ioc, int lpm, int width, double ppm, int phased) { (void) phased; struct chart *c = u; (void) ioc; (void) lpm; c->begun++; c->width = width; c->ppm = ppm; }
static void on_line(void *u, const unsigned char *p, int width)
{
    struct chart *c = u;
    if (c->lines == c->cap) { c->cap = c->cap ? c->cap * 2 : 256; c->px = realloc(c->px, (size_t) c->cap * WEFAX_MAX_WIDTH); }
    memcpy(c->px + (size_t) c->lines * WEFAX_MAX_WIDTH, p, (size_t) width); c->lines++;
}
static void on_end(void *u, enum wefax_end why, int drop) { struct chart *c = u; c->ended++; c->why = why; c->drop = drop; }

/* snr_db: signal to noise in the 252 kHz the receiver delivers; in the 6.4 kHz of the channel it is
   16 dB better. tuning_hz: the receiver's frequency error. */
static struct chart run(int lines, double snr_db, double tuning_hz, double channel_offset, double *offset_measured, const char *png)
{
    struct chart c; struct wefax_events ev = { &c, on_begin, on_line, on_end };
    struct wefax_decoder *d = wefax_new(CHANNEL_OUT_RATE, 120, &ev);
    struct channel *ch = channel_new(channel_offset);
    double total = 1.0 + 5.0 + 30.0 + lines * 0.5 + 5.0 + 8.0, phase = 0;
    double amp = 0.25, sigma = amp / sqrt(2.0) / pow(10.0, snr_db / 20.0);
    long n = (long) (total * CHANNEL_IN_RATE), i;
    short iq[2 * 2520]; float hz[200]; int k = 0;
    memset(&c, 0, sizeof(c));
    srand(11);
    for (i = 0; i < n; i++) {
        double t = (double) i / CHANNEL_IN_RATE;
        double f = channel_offset + tuning_hz + broadcast_hz(t, lines);
        double si, sq;
        phase += 2.0 * M_PI * f / CHANNEL_IN_RATE;
        if (phase > M_PI) phase -= 2.0 * M_PI;
        si = amp * cos(phase) + sigma * gauss(); sq = amp * sin(phase) + sigma * gauss();
        iq[2 * k] = (short) (si * 32767.0); iq[2 * k + 1] = (short) (sq * 32767.0);
        if (++k == 2520) { size_t m = channel_feed(ch, iq, 2520, hz, NULL); wefax_feed(d, hz, m); k = 0; }
    }
    if (offset_measured) *offset_measured = wefax_offset_hz(d);
    if (png != NULL && c.lines > c.drop) png_write_grey(png, c.px, c.width, c.lines - c.drop, WEFAX_MAX_WIDTH, "Wefax test chart, made by the test itself, never transmitted");
    channel_free(ch); wefax_free(d);
    return c;
}

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

int main(int argc, char **argv)
{
    struct chart c; int shift = 0; double e, off = 0;
    const char *dir = argc > 1 ? argv[1] : NULL;
    char path[512];

    /* a strong signal, the receiver tuned 20 kHz below the channel, as the program does */
    if (dir) snprintf(path, sizeof(path), "%s/strong.png", dir);
    c = run(100, 30.0, 0, 20000.0, &off, dir ? path : NULL);
    e = error_of(&c, 90, &shift);
    printf("strong: lines %d drop %d error %.1f shift %d offset %.0f Hz clock %.1f ppm\n", c.lines, c.drop, e, shift, off, c.ppm);
    CHECK(c.begun == 1 && c.ended == 1 && c.why == WEFAX_END_STOP_TONE, "strong signal: one chart, ended by its stop tone");
    CHECK(c.lines - c.drop >= 98 && c.lines - c.drop <= 103, "strong signal: its 100 lines");
    CHECK(e < 14.0 && abs(shift) <= 4, "strong signal: the chart is the one sent");
    CHECK(fabs(off) < 30.0, "strong signal: no frequency error where there is none");
    free(c.px);

    /* a weak one: 0 dB in 252 kHz is 16 dB in the channel */
    if (dir) snprintf(path, sizeof(path), "%s/weak.png", dir);
    c = run(100, 0.0, 0, 20000.0, &off, dir ? path : NULL);
    e = error_of(&c, 90, &shift);
    printf("weak (16 dB in the channel): begun %d ended %d lines %d error %.1f shift %d\n", c.begun, c.ended, c.lines, e, shift);
    CHECK(c.begun == 1 && c.ended == 1, "weak signal: found and ended");
    CHECK(e < 40.0 && abs(shift) <= 4, "weak signal: the chart can be read");
    free(c.px);

    /* the receiver 600 Hz off (an RTL-SDR without correction on 14 MHz) */
    if (dir) snprintf(path, sizeof(path), "%s/off600.png", dir);
    c = run(100, 20.0, 600.0, 20000.0, &off, dir ? path : NULL);
    e = error_of(&c, 90, &shift);
    printf("600 Hz off: measured %.0f Hz, error %.1f shift %d\n", off, e, shift);
    CHECK(c.begun == 1 && c.why == WEFAX_END_STOP_TONE, "600 Hz off: found and ended");
    CHECK(fabs(off - 600.0) < 40.0, "600 Hz off: measured");
    CHECK(e < 18.0 && abs(shift) <= 4, "600 Hz off: the chart is right");
    free(c.px);

    /* the PNG that was written can be read back: signature and size */
    if (dir) {
        unsigned char h[24]; FILE *f;
        snprintf(path, sizeof(path), "%s/strong.png", dir);
        f = fopen(path, "rb");
        CHECK(f != NULL && fread(h, 1, 24, f) == 24 && memcmp(h, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(h + 12, "IHDR", 4) == 0 &&
              ((h[16] << 24) | (h[17] << 16) | (h[18] << 8) | h[19]) == 1810, "PNG written: signature and 1810 points wide");
        if (f) fclose(f);
    }
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
