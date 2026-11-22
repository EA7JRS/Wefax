/*
 * Wefax · tests of the decoder with a signal made here (never transmitted)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../receiver/wefax_decoder.h"

static int checks, failed;
#define CHECK(c, what) do { checks++; if (!(c)) { failed++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

/* The test chart: a point's value from its place. Bars, a diagonal and a grey ramp. */
static double pattern(int x, int y, int width)
{
    if (y < 20) return (x / 100) % 2 ? 1.0 : 0.0;                     /* bars */
    if (y < 40) return (double) x / width;                            /* ramp */
    if (abs(x - y * 4) < 6) return 0.0;                               /* a diagonal on white */
    return 1.0;
}

struct signal { float *hz; size_t n, cap; double fs; };

static void put(struct signal *s, double hz)
{
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 1 << 20; s->hz = realloc(s->hz, s->cap * sizeof(float)); }
    s->hz[s->n++] = (float) hz;
}

/* tone: black and white alternating f times a second, for secs */
static void tone(struct signal *s, double f, double secs, double *t)
{
    long i, n = (long) (secs * s->fs);
    for (i = 0; i < n; i++, *t += 1.0 / s->fs) put(s, fmod(*t * f, 1.0) < 0.5 ? 400.0 : -400.0);
}

/* A whole broadcast. clock: the transmitter's seconds per receiver second (1 + ppm / 1e6). */
static void broadcast(struct signal *s, int ioc, int lpm, int lines, double clock, double start_secs)
{
    double t = 0, line = 60.0 / lpm * clock;
    int width = (int) (ioc * M_PI + 0.5);
    long i, n;
    tone(s, ioc == 288 ? 675.0 : 300.0, start_secs, &t);
    n = (long) (30.0 * s->fs);                                        /* phasing */
    {
        double t0 = t;
        for (i = 0; i < n; i++, t += 1.0 / s->fs) { double p = fmod((t - t0) / line, 1.0); put(s, p < 0.025 || p >= 0.975 ? 400.0 : -400.0); }
        /* the chart begins on a whole line */
        while (fmod((t - t0) / line, 1.0) > 1.5 / (s->fs * line)) { double p = fmod((t - t0) / line, 1.0); put(s, p < 0.025 || p >= 0.975 ? 400.0 : -400.0); t += 1.0 / s->fs; }
    }
    {
        double t0 = t;
        n = (long) (lines * line * s->fs);
        for (i = 0; i < n; i++, t += 1.0 / s->fs) {
            double l = (t - t0) / line; int y = (int) l; int x = (int) ((l - y) * width);
            put(s, pattern(x, y, width) * 800.0 - 400.0);
        }
    }
    tone(s, 450.0, 5.0, &t);
    n = (long) (10.0 * s->fs);
    for (i = 0; i < n; i++) put(s, -400.0);
}

struct chart { int begun, ended, ioc, lpm, width, lines, drop; enum wefax_end why; double ppm; unsigned char *px; int cap; };

static void on_begin(void *u, int ioc, int lpm, int width, double ppm, int phased) { (void) phased; struct chart *c = u; c->begun++; c->ioc = ioc; c->lpm = lpm; c->width = width; c->ppm = ppm; c->lines = 0; }
static void on_line(void *u, const unsigned char *p, int width)
{
    struct chart *c = u;
    if (c->lines == c->cap) { c->cap = c->cap ? c->cap * 2 : 256; c->px = realloc(c->px, (size_t) c->cap * WEFAX_MAX_WIDTH); }
    memcpy(c->px + (size_t) c->lines * WEFAX_MAX_WIDTH, p, (size_t) width);
    c->lines++;
}
static void on_end(void *u, enum wefax_end why, int drop) { struct chart *c = u; c->ended++; c->why = why; c->drop = drop; }

/* Mean absolute difference between what was decoded and the test chart, in grey levels, over the
   first `lines` lines; shift: how many points the decoded chart may be displaced sideways. */
static double error_of(const struct chart *c, int lines, int *best_shift)
{
    double best = 1e9; int sh;
    for (sh = -6; sh <= 6; sh++) {
        double sum = 0; long n = 0; int y, x;
        for (y = 0; y < lines && y < c->lines; y++)
            for (x = 8; x < c->width - 8; x++) {
                sum += fabs(c->px[(size_t) y * WEFAX_MAX_WIDTH + x + sh] - 255.0 * pattern(x, y, c->width)); n++;
            }
        if (n > 0 && sum / n < best) { best = sum / n; if (best_shift) *best_shift = sh; }
    }
    return best;
}

static double tuning_error_hz;       /* added to every sample: a receiver off frequency */
static double measured_offset_hz;

static struct chart run(int ioc, int lpm, int lines, double ppm, double noise_hz, int hint, double start_secs)
{
    struct signal s = { NULL, 0, 0, 12000.0 };
    struct chart c; struct wefax_events ev = { &c, on_begin, on_line, on_end };
    struct wefax_decoder *d;
    size_t i;
    memset(&c, 0, sizeof(c));
    /* some silence (noise) first, as on the air */
    srand(7);
    for (i = 0; i < 24000; i++) put(&s, (rand() / (double) RAND_MAX - 0.5) * 6000.0);
    broadcast(&s, ioc, lpm, lines, 1.0 + ppm / 1e6, start_secs);
    if (tuning_error_hz != 0) for (i = 0; i < s.n; i++) s.hz[i] += (float) tuning_error_hz;
    if (noise_hz > 0) for (i = 0; i < s.n; i++) s.hz[i] += (float) ((rand() / (double) RAND_MAX - 0.5) * 2.0 * noise_hz);
    d = wefax_new(s.fs, hint, &ev);
    /* in pieces of odd sizes, as a receiver delivers them */
    for (i = 0; i < s.n; ) { size_t n = 997 + (i % 500); if (i + n > s.n) n = s.n - i; wefax_feed(d, s.hz + i, n); i += n; }
    measured_offset_hz = wefax_offset_hz(d);
    wefax_free(d);
    free(s.hz);
    return c;
}

int main(void)
{
    struct chart c;
    int shift = 0;
    double e;

    /* the usual broadcast: IOC 576 at 120 lines a minute */
    c = run(576, 120, 120, 0, 0, 120, 5.0);
    CHECK(c.begun == 1 && c.ended == 1, "one chart, begun and ended");
    CHECK(c.ioc == 576 && c.lpm == 120 && c.width == 1810, "IOC 576, 120 LPM, 1810 points");
    CHECK(c.why == WEFAX_END_STOP_TONE, "ended by the stop tone");
    CHECK(c.lines - c.drop >= 118 && c.lines - c.drop <= 122, "about the 120 lines sent");
    e = error_of(&c, 100, &shift);
    printf("576/120: lines %d (drop %d), error %.1f, shift %d, clock %.1f ppm\n", c.lines, c.drop, e, shift, c.ppm);
    CHECK(e < 6.0, "the chart is the one sent");
    CHECK(abs(shift) <= 2, "and begins where it should");
    CHECK(fabs(c.ppm) < 5.0, "no clock error measured where there is none");
    free(c.px);

    /* IOC 288, by its 675 Hz start tone */
    c = run(288, 120, 60, 0, 0, 120, 5.0);
    CHECK(c.begun == 1 && c.ioc == 288 && c.width == 905, "IOC 288: 905 points");
    e = error_of(&c, 50, &shift);
    CHECK(e < 8.0 && abs(shift) <= 2, "IOC 288 chart");
    free(c.px);

    /* 60 and 90 lines a minute, told apart by the phasing even when 120 was expected */
    c = run(576, 60, 40, 0, 0, 120, 5.0);
    printf("576/60: lpm %d lines %d error %.1f\n", c.lpm, c.lines, error_of(&c, 30, &shift));
    CHECK(c.begun == 1 && c.lpm == 60, "60 LPM found by the phasing");
    CHECK(error_of(&c, 30, &shift) < 6.0 && abs(shift) <= 2, "60 LPM chart");
    free(c.px);
    c = run(576, 90, 60, 0, 0, 120, 5.0);
    CHECK(c.begun == 1 && c.lpm == 90, "90 LPM found by the phasing");
    CHECK(error_of(&c, 50, &shift) < 6.0 && abs(shift) <= 2, "90 LPM chart");
    free(c.px);
    c = run(576, 240, 200, 0, 0, 120, 5.0);
    CHECK(c.begun == 1 && c.lpm == 240, "240 LPM found by the phasing");
    CHECK(error_of(&c, 150, &shift) < 9.0 && abs(shift) <= 3, "240 LPM chart");
    free(c.px);

    /* a receiver whose clock is 60 parts per million fast or slow (an RTL-SDR without correction):
       without measuring it the chart would lean 1 point every 9 lines */
    c = run(576, 120, 300, 60.0, 0, 120, 5.0);
    e = error_of(&c, 280, &shift);
    printf("+60 ppm: measured %.1f ppm, error %.1f, shift %d\n", c.ppm, e, shift);
    CHECK(fabs(c.ppm - 60.0) < 8.0, "clock error of +60 ppm measured");
    CHECK(e < 7.0, "and the chart does not lean");
    free(c.px);
    c = run(576, 120, 300, -45.0, 0, 120, 5.0);
    e = error_of(&c, 280, &shift);
    CHECK(fabs(c.ppm + 45.0) < 8.0 && e < 7.0, "clock error of -45 ppm measured and corrected");
    free(c.px);

    /* with noise on the frequency (a weak signal) */
    c = run(576, 120, 120, 0, 250.0, 120, 5.0);
    e = error_of(&c, 100, &shift);
    printf("noise: begun %d ended %d lines %d error %.1f shift %d\n", c.begun, c.ended, c.lines, e, shift);
    CHECK(c.begun == 1 && c.ended == 1 && c.why == WEFAX_END_STOP_TONE, "with noise: found and ended");
    CHECK(e < 45.0 && abs(shift) <= 2, "with noise: the chart is there and in place");
    free(c.px);

    /* tuned in when the start tone was already half gone */
    c = run(576, 120, 80, 0, 0, 120, 2.2);
    CHECK(c.begun == 1 && error_of(&c, 60, &shift) < 6.0, "a short start tone is enough");
    free(c.px);

    /* a receiver 700 Hz off frequency (an RTL-SDR 50 ppm out at 14 MHz): black and white would both
       fall outside the scale; the start tone tells where the channel really is */
    tuning_error_hz = 700.0;
    c = run(576, 120, 120, 0, 0, 120, 5.0);
    e = error_of(&c, 100, &shift);
    printf("700 Hz off: measured %.0f Hz, error %.1f\n", measured_offset_hz, e);
    CHECK(c.begun == 1 && c.why == WEFAX_END_STOP_TONE, "700 Hz off: found and ended");
    CHECK(fabs(measured_offset_hz - 700.0) < 25.0, "700 Hz off: measured");
    CHECK(e < 8.0 && abs(shift) <= 2, "700 Hz off: the chart is right");
    free(c.px);
    tuning_error_hz = -550.0;
    c = run(288, 120, 80, 30.0, 150.0, 120, 5.0);
    CHECK(c.begun == 1 && fabs(measured_offset_hz + 550.0) < 40.0 && error_of(&c, 60, &shift) < 40.0, "-550 Hz off, clock error and noise at once");
    free(c.px);
    tuning_error_hz = 0;

    /* noise alone starts nothing */
    {
        struct chart n; struct wefax_events ev = { &n, on_begin, on_line, on_end };
        struct wefax_decoder *d = wefax_new(12000.0, 120, &ev);
        float buf[1200]; int k, i;
        memset(&n, 0, sizeof(n));
        srand(3);
        for (k = 0; k < 600; k++) { for (i = 0; i < 1200; i++) buf[i] = (float) ((rand() / (double) RAND_MAX - 0.5) * 6000.0); wefax_feed(d, buf, 1200); }
        CHECK(n.begun == 0 && wefax_state(d) == WEFAX_IDLE, "a minute of noise starts no chart");
        /* a broadcast caught half way: forced, it gives lines until it is flushed */
        wefax_force_image(d, 576, 120);
        for (k = 0; k < 100; k++) { for (i = 0; i < 1200; i++) buf[i] = 400.0f; wefax_feed(d, buf, 1200); }
        CHECK(n.begun == 1 && n.lines >= 19 && n.lines <= 21, "forced: 10 s give 20 lines");
        wefax_flush(d);
        CHECK(n.ended == 1 && n.why == WEFAX_END_FLUSHED && wefax_state(d) == WEFAX_IDLE, "flushed");
        wefax_free(d); free(n.px);
    }
    /* a chart whose signal goes away ends by itself, without the lines of noise */
    {
        struct signal s = { NULL, 0, 0, 12000.0 };
        struct chart n; struct wefax_events ev = { &n, on_begin, on_line, on_end };
        struct wefax_decoder *d = wefax_new(12000.0, 120, &ev);
        size_t i;
        memset(&n, 0, sizeof(n));
        broadcast(&s, 576, 120, 100, 1.0, 5.0);
        s.n -= (size_t) (15.0 * 12000);                              /* without its stop tone */
        srand(5);
        for (i = 0; i < 12000 * 40; i++) put(&s, (rand() / (double) RAND_MAX - 0.5) * 6000.0);
        wefax_feed(d, s.hz, s.n);
        printf("lost: ended %d why %d lines %d drop %d\n", n.ended, n.why, n.lines, n.drop);
        CHECK(n.begun == 1 && n.ended == 1 && n.why == WEFAX_END_SIGNAL_LOST, "signal lost ends the chart");
        CHECK(n.lines - n.drop >= 96 && n.lines - n.drop <= 104, "and the noise is not part of it");
        wefax_free(d); free(s.hz); free(n.px);
    }

    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
