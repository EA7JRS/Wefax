/*
 * Wefax · from the receiver's samples to the frequency of the facsimile channel (see channel.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "channel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAPS1 49                      /* 252 kS/s -> 36 kS/s, keeps 0 to 8 kHz */
#define DEC1 7
#define TAPS2 95                      /* 36 kS/s -> 12 kS/s, keeps the channel */
#define DEC2 3

struct fir { int taps, dec, pos, count; double *h; double *zi, *zq; };

struct channel {
    double phase, step;               /* oscillator that moves the channel to the centre */
    struct fir f1, f2;
    double pi_, pq_;                  /* the previous output sample, for the discriminator */
    double level;
};

/* Low-pass by a windowed sinc (Hamming). cutoff as a fraction of the sample rate. */
static int fir_init(struct fir *f, int taps, int dec, double cutoff)
{
    int i; double sum = 0;
    f->taps = taps; f->dec = dec; f->pos = 0; f->count = 0;
    f->h = malloc(sizeof(double) * taps);
    f->zi = calloc((size_t) taps * 2, sizeof(double));
    f->zq = calloc((size_t) taps * 2, sizeof(double));
    if (f->h == NULL || f->zi == NULL || f->zq == NULL) return -1;
    for (i = 0; i < taps; i++) {
        double x = i - (taps - 1) / 2.0;
        double s = x == 0 ? 2.0 * cutoff : sin(2.0 * M_PI * cutoff * x) / (M_PI * x);
        f->h[i] = s * (0.54 - 0.46 * cos(2.0 * M_PI * i / (taps - 1)));
        sum += f->h[i];
    }
    for (i = 0; i < taps; i++) f->h[i] /= sum;
    return 0;
}

static void fir_free(struct fir *f) { free(f->h); free(f->zi); free(f->zq); }

/* Takes one sample; returns 1 when an output sample is ready in *oi, *oq */
static int fir_put(struct fir *f, double i, double q, double *oi, double *oq)
{
    /* the history is kept twice, so the last `taps` samples are always in a row */
    f->zi[f->pos] = f->zi[f->pos + f->taps] = i;
    f->zq[f->pos] = f->zq[f->pos + f->taps] = q;
    f->pos = (f->pos + 1) % f->taps;
    if (++f->count < f->dec) return 0;
    f->count = 0;
    {
        const double *zi = f->zi + f->pos, *zq = f->zq + f->pos, *h = f->h;
        double si = 0, sq = 0; int k;
        for (k = 0; k < f->taps; k++) { si += h[k] * zi[k]; sq += h[k] * zq[k]; }
        *oi = si; *oq = sq;
    }
    return 1;
}

struct channel *channel_new(double offset_hz)
{
    struct channel *c = calloc(1, sizeof(*c));
    if (c == NULL) return NULL;
    c->step = -2.0 * M_PI * offset_hz / CHANNEL_IN_RATE;
    if (fir_init(&c->f1, TAPS1, DEC1, 9000.0 / CHANNEL_IN_RATE) != 0 ||
        fir_init(&c->f2, TAPS2, DEC2, CHANNEL_HALF_WIDTH_HZ / (CHANNEL_IN_RATE / DEC1)) != 0) { channel_free(c); return NULL; }
    return c;
}

void channel_free(struct channel *c)
{
    if (c == NULL) return;
    fir_free(&c->f1); fir_free(&c->f2);
    free(c);
}

size_t channel_feed(struct channel *c, const short *iq, size_t n_pairs, float *out, double *level_out)
{
    size_t n, made = 0;
    for (n = 0; n < n_pairs; n++) {
        double cs = cos(c->phase), sn = sin(c->phase);
        double i = iq[2 * n] / 32768.0, q = iq[2 * n + 1] / 32768.0;
        double mi = i * cs - q * sn, mq = i * sn + q * cs;
        double ai, aq, bi, bq;
        c->phase += c->step;
        if (c->phase > M_PI) c->phase -= 2.0 * M_PI; else if (c->phase < -M_PI) c->phase += 2.0 * M_PI;
        if (!fir_put(&c->f1, mi, mq, &ai, &aq)) continue;
        if (!fir_put(&c->f2, ai, aq, &bi, &bq)) continue;
        {
            /* the angle turned since the previous sample is the frequency */
            double re = bi * c->pi_ + bq * c->pq_, im = bq * c->pi_ - bi * c->pq_;
            out[made++] = (float) (atan2(im, re) * CHANNEL_OUT_RATE / (2.0 * M_PI));
            c->level += (sqrt(bi * bi + bq * bq) - c->level) * 0.001;
            c->pi_ = bi; c->pq_ = bq;
        }
    }
    if (level_out != NULL) *level_out = c->level;
    return made;
}
