/*
 * Wefax · checks of the VOACAP input and of the reading of its output
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * test/voacap_sample.out is the start of a real output of voacapl 16.1207W (Pinneberg to Cadiz,
 * November 2026, sunspot number 80). The program itself is not needed for these checks.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../web_server/voacap.h"

#ifndef SAMPLE
#define SAMPLE "../test/voacap_sample.out"
#endif
static int checks, failed;
#define CHECK(c, what) do { checks++; if (!(c)) { failed++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

int main(void)
{
    struct voacap_in in = { 53.67, 9.80, 36.53, -6.29, 10, { 3855, 7880, 13882.5 }, 3, 2026, 11, 80, 145, 40 };
    static struct voacap_out out;
    static char text[8000], sample[20000];
    FILE *f;
    size_t n;

    CHECK(voacap_input(&in, text, sizeof(text)) > 0, "an input file is written");
    CHECK(strstr(text, "CIRCUIT   53.67N     9.80E    36.53N     6.29W  S     0\n") != NULL, "the circuit card, in its columns");
    CHECK(strstr(text, "MONTH      2026 11.00\n") != NULL && strstr(text, "SUNSPOT     80.\n") != NULL, "month and sunspot number");
    CHECK(strstr(text, "FREQUENCY  3.85 7.8813.88 0.00") != NULL || strstr(text, "FREQUENCY  3.86 7.8813.88 0.00") != NULL, "the frequencies in MHz, five columns each");
    CHECK(strstr(text, "]  0.0   10.0000\n") != NULL && strstr(text, "SYSTEM       1. 145. 3.00  90. 40.0 3.00 0.10\n") != NULL, "power, noise and ratio needed");
    in.tx_lat = -33.0; in.tx_lon = -71.6;
    CHECK(voacap_input(&in, text, sizeof(text)) > 0 && strstr(text, "CIRCUIT   33.00S    71.60W") != NULL, "south and west");
    in.khz[0] = 518;
    CHECK(voacap_input(&in, text, sizeof(text)) < 0, "medium wave is refused: VOACAP is for 2 to 30 MHz");
    in.khz[0] = 3855; in.ssn = 400;
    CHECK(voacap_input(&in, text, sizeof(text)) < 0, "an impossible sunspot number is refused");
    in.ssn = 80; in.nfreqs = 12;
    CHECK(voacap_input(&in, text, sizeof(text)) < 0, "eleven frequencies at most");

    f = fopen(SAMPLE, "rb");
    CHECK(f != NULL, "the sample output is there");
    if (f != NULL) {
        n = fread(sample, 1, sizeof(sample) - 1, f); sample[n] = '\0';
        fclose(f);
        CHECK(voacap_parse(sample, 3, &out) == 2, "two hours in the sample");
        CHECK(fabs(out.muf[1] - 10.3) < 0.01 && fabs(out.muf[2] - 10.8) < 0.01, "the MUF of each hour");
        CHECK(fabs(out.rel[0][1] - 0.98) < 0.001 && fabs(out.rel[1][1] - 0.99) < 0.001 && fabs(out.rel[2][1] - 0.56) < 0.001, "reliability at 01 UTC, by frequency");
        CHECK(fabs(out.snr[0][1] - 60) < 0.1 && fabs(out.snr[2][2] - 21) < 0.1, "the signal to noise row, not the SNR LW or SNR UP ones");
        CHECK(fabs(out.rel[2][2] - 0.17) < 0.001 && isnan(out.rel[0][5]) && isnan(out.muf[0]), "hours not in the output stay unknown");
    }
    voacap_set_dir("/nonexistent");
    CHECK(!voacap_available() && voacap_run(&in, &out) == -1, "without the program there are no predictions");
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
