/*
 * Wefax receiver: low-pass filter and decimation by 8 for a wideband receiver.
 *
 * The filter is a sinc with a Kaiser window, 64 coefficients, cut at 100 kHz: it keeps the
 * ±50 kHz around the centre untouched and takes more than 70 dB off everything that would fold
 * onto them (from 202 kHz up). Its gain is 256, so the 8 bit samples fill the 16 bits.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "decim.h"

#define TAPS 64

struct decim {
    float h[TAPS];
    float hi[2 * TAPS], hq[2 * TAPS];   /* the last samples, written twice so a run of TAPS is always in one piece */
    int pos, phase;
};

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0; int k;
    for (k = 1; k < 40; k++) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
    return sum;
}

struct decim *decim_new(void)
{
    struct decim *d = calloc(1, sizeof(*d));
    const double cut = 100000.0 / DECIM_IN_RATE, beta = 7.0;
    double sum = 0; int i;
    if (d == NULL) return NULL;
    for (i = 0; i < TAPS; i++) {
        double x = i - (TAPS - 1) / 2.0, r = 2.0 * x / (TAPS - 1);
        double s = 2.0 * cut * (fabs(x) < 1e-9 ? 1.0 : sin(2.0 * M_PI * cut * x) / (2.0 * M_PI * cut * x));
        double w = bessel_i0(beta * sqrt(1.0 - r * r)) / bessel_i0(beta);
        d->h[i] = (float) (s * w); sum += s * w;
    }
    for (i = 0; i < TAPS; i++) d->h[i] = (float) (d->h[i] * 256.0 / sum);
    return d;
}

void decim_free(struct decim *d) { free(d); }

size_t decim_feed(struct decim *d, const signed char *iq, size_t n_pairs, short *out)
{
    size_t i, made = 0;
    for (i = 0; i < n_pairs; i++) {
        float vi = iq[2 * i], vq = iq[2 * i + 1];
        d->hi[d->pos] = d->hi[d->pos + TAPS] = vi;
        d->hq[d->pos] = d->hq[d->pos + TAPS] = vq;
        if (++d->pos == TAPS) d->pos = 0;
        if (++d->phase == DECIM_FACTOR) {
            const float *pi = d->hi + d->pos, *pq = d->hq + d->pos;
            float si = 0, sq = 0; int k;
            d->phase = 0;
            for (k = 0; k < TAPS; k++) { si += d->h[k] * pi[k]; sq += d->h[k] * pq[k]; }
            out[2 * made] = (short) (si > 32767.f ? 32767 : si < -32768.f ? -32768 : lrintf(si));
            out[2 * made + 1] = (short) (sq > 32767.f ? 32767 : sq < -32768.f ? -32768 : lrintf(sq));
            made++;
        }
    }
    return made;
}
