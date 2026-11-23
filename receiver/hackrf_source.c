/*
 * Wefax receiver: HackRF One source.
 *
 * A HackRF One tunes from 1 MHz up, so it reaches the facsimile channels of HF without a
 * converter (below 1 MHz it does not: the channels of 100 to 150 kHz are out of its reach).
 * Its slowest advisable rate is 2 MS/s, so it is asked for 2.016 MS/s, 20 kHz below the channel,
 * and one sample of every 8 is kept after a low-pass filter (decim.c): the 252 kS/s of the rest.
 * It has no automatic gain: the IF gain (0 to 40 dB in steps of 8) and the baseband gain (0 to
 * 62 dB in steps of 2) are set by hand, and its RF amplifier is best left off on HF with a long
 * wire. Its samples are 8 bit: with strong stations nearby an HF band-pass filter helps.
 * It only receives: nothing here ever switches the HackRF to transmit.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "iq_source.h"

#ifdef HAVE_LIBHACKRF
#include <libhackrf/hackrf.h>
#include "decim.h"

static struct decim *dec8;

static int rx_callback(hackrf_transfer *t)
{
    static short out[2 * 4096];
    const signed char *p = (const signed char *) t->buffer;
    size_t pairs = (size_t) t->valid_length / 2;
    if (stop_requested) return 0;
    while (pairs > 0) {
        static short bi[4096], bq[4096];
        size_t n = pairs > 4096 * DECIM_FACTOR ? 4096 * DECIM_FACTOR : pairs, m, i;
        m = decim_feed(dec8, p, n, out);
        for (i = 0; i < m; i++) { bi[i] = out[2 * i]; bq[i] = out[2 * i + 1]; }
        iq_push_short(bi, bq, (unsigned int) m);
        p += 2 * n; pairs -= n;
    }
    return 0;
}

static int step(int v, int max, int by) { if (v < 0) v = 0; if (v > max) v = max; return v - v % by; }

int hackrf_source_run(void)
{
    hackrf_device *dev = NULL;
    uint64_t freq = (uint64_t) iq_tuned_hz;
    int lna = step(iq_opt.hackrf_lna_db, 40, 8), vga = step(iq_opt.hackrf_vga_db, 62, 2);
    int failed = 1, r;

    if (iq_tuned_hz < 1000000) {
        printf("hackrf: %ld Hz is below what a HackRF One tunes (1 MHz)\n", iq_tuned_hz + IQ_TUNE_BELOW_HZ);
        return 1;
    }
    if ((r = hackrf_init()) != HACKRF_SUCCESS) {
        printf("hackrf: cannot start the library: %s\n", hackrf_error_name(r));
        return 1;
    }
    r = iq_opt.hackrf_serial != NULL && iq_opt.hackrf_serial[0] != '\0' ? hackrf_open_by_serial(iq_opt.hackrf_serial, &dev) : hackrf_open(&dev);
    if (r != HACKRF_SUCCESS) {
        printf("hackrf: cannot open the HackRF: %s. Is it plugged in, in use by another program, or missing its udev rule?\n", hackrf_error_name(r));
        hackrf_exit();
        return 1;
    }
    if ((r = hackrf_set_sample_rate(dev, (double) DECIM_IN_RATE)) != HACKRF_SUCCESS) { printf("hackrf: cannot set the sample rate: %s\n", hackrf_error_name(r)); goto close_dev; }
    /* the narrowest filter of the chip (1.75 MHz) is just under the sample rate */
    if ((r = hackrf_set_baseband_filter_bandwidth(dev, hackrf_compute_baseband_filter_bw_round_down_lt(DECIM_IN_RATE))) != HACKRF_SUCCESS) { printf("hackrf: cannot set the baseband filter: %s\n", hackrf_error_name(r)); goto close_dev; }
    if ((r = hackrf_set_freq(dev, freq)) != HACKRF_SUCCESS) { printf("hackrf: cannot tune to %llu Hz: %s\n", (unsigned long long) freq, hackrf_error_name(r)); goto close_dev; }
    if (hackrf_set_amp_enable(dev, iq_opt.hackrf_amp ? 1 : 0) != HACKRF_SUCCESS || hackrf_set_lna_gain(dev, (uint32_t) lna) != HACKRF_SUCCESS || hackrf_set_vga_gain(dev, (uint32_t) vga) != HACKRF_SUCCESS) { printf("hackrf: cannot set the gains\n"); goto close_dev; }
    if (hackrf_set_antenna_enable(dev, iq_opt.bias_t ? 1 : 0) != HACKRF_SUCCESS && iq_opt.bias_t) printf("hackrf: cannot switch the antenna power on (continuing)\n");
    printf("hackrf: tuned to %llu Hz, %d S/s, IF gain %d dB, baseband gain %d dB, RF amplifier %s%s\n", (unsigned long long) freq, DECIM_IN_RATE, lna, vga, iq_opt.hackrf_amp ? "on" : "off", iq_opt.bias_t ? ", antenna power on" : "");

    if ((dec8 = decim_new()) == NULL || iq_ring_init() != 0) { printf("out of memory\n"); decim_free(dec8); dec8 = NULL; goto close_dev; }
    if ((r = hackrf_start_rx(dev, rx_callback, NULL)) != HACKRF_SUCCESS) {
        printf("hackrf: cannot start receiving: %s\n", hackrf_error_name(r));
        iq_ring_free(); decim_free(dec8); dec8 = NULL;
        goto close_dev;
    }

    consume_loop();                    /* no samples for a while counts as lost: an unplugged HackRF ends here */
    if (hackrf_is_streaming(dev) != HACKRF_TRUE && !stop_requested && !retune_requested) device_lost = 1;
    failed = device_lost ? 1 : 0;
    if (stop_requested) printf("Stop requested - releasing the receiver\n");

    hackrf_stop_rx(dev);
    iq_ring_free();
    decim_free(dec8); dec8 = NULL;
    if (iq_opt.bias_t) hackrf_set_antenna_enable(dev, 0);

close_dev:
    hackrf_close(dev);
    hackrf_exit();
    return failed;
}

#else  /* !HAVE_LIBHACKRF */

int hackrf_source_run(void)
{
    printf("This build has no HackRF support (libhackrf-dev was not found when it was compiled)\n");
    return 1;
}

#endif
