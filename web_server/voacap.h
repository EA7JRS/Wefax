/*
 * Wefax · predictions of a short wave circuit with VOACAP
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * VOACAP (NTIA / ITS, United States; not subject to copyright there) is run as a separate
 * program, voacapl, its port to Linux by J. A. Watson. Nothing of it is included here: when it
 * is not installed there are simply no predictions.
 */
#ifndef WEFAX_VOACAP_H
#define WEFAX_VOACAP_H

#define VOACAP_MAX_FREQS 11

struct voacap_in {
    double tx_lat, tx_lon, rx_lat, rx_lon;
    double kw;                  /* transmitter power */
    double khz[VOACAP_MAX_FREQS];
    int nfreqs;
    int year, month;            /* the prediction is for a month: its median day */
    double ssn;                 /* smoothed sunspot number of that month */
    double noise;               /* man-made noise at 3 MHz in -dBW: 145 residential, 150 rural... */
    double snr;                 /* signal to noise ratio needed, dB in 1 Hz */
};

struct voacap_out {
    /* [frequency][UTC hour 0..23] */
    double rel[VOACAP_MAX_FREQS][24];    /* 0..1: fraction of the days of the month the ratio is reached */
    double snr[VOACAP_MAX_FREQS][24];    /* median signal to noise ratio, dB in 1 Hz */
    double muf[24];                      /* median maximum usable frequency of the path, MHz */
};

/* The folder with VOACAP's data (itshfbc). Call before voacap_run. */
void voacap_set_dir(const char *itshfbc);
/* 1 when the program and its data are there */
int voacap_available(void);
/* Runs one prediction. 0 done; -1 not installed; -2 bad input; -3 it failed or its output was not understood. */
int voacap_run(const struct voacap_in *in, struct voacap_out *out);
/* The text of the input file for a prediction (for the tests). Returns the length, or -1. */
int voacap_input(const struct voacap_in *in, char *text, int size);
/* Reads the output of the program. Returns the number of hours found (24 when complete). */
int voacap_parse(const char *text, int nfreqs, struct voacap_out *out);

#endif
