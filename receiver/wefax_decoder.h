/*
 * Wefax · radiofacsimile decoder: from the frequency of the signal to an image
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * A weather chart on HF is sent as frequency modulation: black is 400 Hz below the centre of the
 * channel and white 400 Hz above (WMO Manual on the Global Telecommunication System, WMO-No. 386,
 * part III; ITU-R F.1 class F3C). Each broadcast is:
 *
 *   start tone   5 s of black and white alternating 300 times a second (index of cooperation 576)
 *                or 675 times a second (index 288)
 *   phasing      30 s of lines that are black except for a white pulse of 5 % of the line; the
 *                middle of the pulse is where a line begins
 *   the chart    lines at 120 a minute (also 60, 90 and 240), each IOC x pi points long
 *   stop tone    5 s alternating 450 times a second, then 10 s of black
 *
 * This decoder takes the instantaneous frequency of the channel, sample by sample, finds those
 * parts and delivers the chart line by line. It measures the real length of a line during the
 * phasing, so a receiver whose clock is off by some parts per million does not slant the chart.
 * The tones and the phasing pulse are as fldigi (W1HKJ and others, GPL-3) describes them; the
 * code is new.
 */
#ifndef WEFAX_DECODER_H
#define WEFAX_DECODER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEFAX_DEVIATION_HZ 400.0      /* black = centre - 400 Hz, white = centre + 400 Hz */
#define WEFAX_START_576_HZ 300.0
#define WEFAX_START_288_HZ 675.0
#define WEFAX_STOP_HZ 450.0
#define WEFAX_MAX_WIDTH 1810          /* IOC 576: 576 x pi points in a line */

enum wefax_state { WEFAX_IDLE, WEFAX_START, WEFAX_PHASING, WEFAX_IMAGE };

/* What ended a chart */
enum wefax_end { WEFAX_END_STOP_TONE, WEFAX_END_TOO_LONG, WEFAX_END_SIGNAL_LOST, WEFAX_END_FLUSHED };

struct wefax_events {
    void *user;
    /* a chart begins: index of cooperation, lines per minute, points per line, and how far the
       receiver's clock is from the transmitter's, in parts per million (0 when not measured).
       phased: 1 when the start of the lines was found in the phasing; 0 when the chart is taken
       as it comes (no phasing found, or forced) and may be shifted sideways */
    void (*begin)(void *user, int ioc, int lpm, int width, double clock_ppm, int phased);
    /* one line of the chart: width values, 0 black to 255 white */
    void (*line)(void *user, const unsigned char *pixels, int width);
    /* the chart ended; lines_to_drop: lines at the end that are the stop tone, not the chart */
    void (*end)(void *user, enum wefax_end why, int lines_to_drop);
};

struct wefax_decoder;

/* sample_rate: values per second given to wefax_feed(). lpm: lines per minute expected (120 when
   0); the phasing tells 60, 90, 120 and 240 apart when it can. Returns NULL without memory. */
struct wefax_decoder *wefax_new(double sample_rate, int lpm, const struct wefax_events *events);
void wefax_free(struct wefax_decoder *d);

/* freq_hz: frequency of each sample relative to the centre of the channel (-400 black, +400 white) */
void wefax_feed(struct wefax_decoder *d, const float *freq_hz, size_t n);

/* Ends a chart in progress (the broadcast time is over, or the receiver is needed elsewhere) */
void wefax_flush(struct wefax_decoder *d);

/* Starts a chart now, without start tone or phasing: the lines begin wherever they fall, so the
   chart comes out shifted sideways. For a broadcast caught half way. */
void wefax_force_image(struct wefax_decoder *d, int ioc, int lpm);

/* Black and white at +-hz from the centre instead of 400 (the Deutscher Wetterdienst uses 425) */
void wefax_set_deviation(struct wefax_decoder *d, double hz);
/* Lines per minute expected from now on (from the schedule) */
void wefax_set_lpm(struct wefax_decoder *d, int lpm);
/* Lines of the chart in progress, 0 when there is none */
int wefax_lines(const struct wefax_decoder *d);
enum wefax_state wefax_state(const struct wefax_decoder *d);
/* How far the receiver is from the centre of the channel, in Hz, as measured on the last start tone
   (a receiver's own frequency error); every chart is corrected by it. It can also be set by hand. */
double wefax_offset_hz(const struct wefax_decoder *d);
void wefax_set_offset_hz(struct wefax_decoder *d, double hz);
/* Strength of the three tones in the last half second, 0 to 1 (for the status page and tests) */
void wefax_tones(const struct wefax_decoder *d, double *start576, double *start288, double *stop);

#ifdef __cplusplus
}
#endif
#endif
