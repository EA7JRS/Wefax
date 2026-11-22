/*
 * Wefax · from the receiver's samples to the frequency of the facsimile channel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * The receivers deliver complex samples at 252 kS/s. The facsimile channel is a few kilohertz
 * wide somewhere in that band: it is moved to the centre, filtered, brought down to 12 kS/s and
 * its frequency taken sample by sample (a frequency discriminator).
 */
#ifndef WEFAX_CHANNEL_H
#define WEFAX_CHANNEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHANNEL_IN_RATE 252000        /* samples per second from the receiver */
#define CHANNEL_OUT_RATE 12000        /* frequency values per second to the decoder */
#define CHANNEL_HALF_WIDTH_HZ 3200.0  /* what is kept on each side of the centre */

struct channel;

/* offset_hz: where the centre of the facsimile channel is in the receiver's band (the receiver is
   tuned a little to one side, away from its own carrier at zero). Returns NULL without memory. */
struct channel *channel_new(double offset_hz);
void channel_free(struct channel *c);

/* iq: pairs I, Q of 16 bit samples. out receives the frequency in Hz of each output sample, at
   most n_pairs / 21 + 1 values. level_out (may be NULL): mean amplitude of the channel, 0 to 1.
   Returns how many values were written. */
size_t channel_feed(struct channel *c, const short *iq, size_t n_pairs, float *out, double *level_out);

#ifdef __cplusplus
}
#endif
#endif
