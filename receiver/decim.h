/*
 * Wefax receiver: from the fast samples of a wideband receiver (HackRF: 8 bit, 2.016 MS/s) to the
 * 252 kS/s the rest of the program works with. A low-pass filter and one sample kept of every 8.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef WEFAX_DECIM_H
#define WEFAX_DECIM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DECIM_FACTOR 8
#define DECIM_IN_RATE (252000 * DECIM_FACTOR)     /* 2 016 000 samples per second asked of the receiver */

struct decim;

struct decim *decim_new(void);
void decim_free(struct decim *d);

/* iq: pairs I, Q of signed 8 bit samples. out: pairs I, Q of 16 bit samples, room for
   n_pairs / 8 + 1 pairs. Returns how many pairs were written. */
size_t decim_feed(struct decim *d, const signed char *iq, size_t n_pairs, short *out);

#ifdef __cplusplus
}
#endif
#endif
