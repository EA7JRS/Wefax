/*
 * Wefax · makes a recording of a facsimile broadcast as a receiver would deliver it, to try the
 * receiver program without a radio. The chart is a test pattern; nothing here is ever transmitted.
 *
 *   make_recording out.wav [lines] [snr dB] [receiver error Hz]
 *
 * 252000 S/s, 16 bit I and Q, the channel 20 kHz above the centre.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 252000
#define WIDTH 1810

/* A pattern that looks like a chart: frame, grid, isobar-like curves and the word TEST in blocks */
static double pattern(int x, int y, int lines)
{
    static const char *word[5] = { "###.###.###.###", ".#..#...#....#.", ".#..##..###..#.", ".#..#.....#..#.", ".#..###.###..#." };
    double cx = x - WIDTH / 2.0, cy = (y - lines / 2.0) * 3.0, r = sqrt(cx * cx + cy * cy);
    if (x < 12 || x >= WIDTH - 12 || y < 4 || y >= lines - 4) return 0.0;             /* frame */
    if (y >= 20 && y < 60 && x >= 100 && x < 100 + 15 * 24) return word[(y - 20) / 8][(x - 100) / 24] == '#' ? 0.0 : 1.0;
    if (x % 300 < 2 || y % 100 < 1) return 0.55;                                       /* grid */
    if (fmod(r, 90.0) < 2.5) return 0.0;                                               /* circles */
    if (y > lines - 40) return (double) x / WIDTH;                                     /* grey scale */
    return 1.0;
}

static double hz_at(double t, int lines)
{
    const double line = 0.5;
    if (t < 2.0) return 0;
    t -= 2.0;
    if (t < 5.0) return fmod(t * 300.0, 1.0) < 0.5 ? 400.0 : -400.0;
    t -= 5.0;
    if (t < 30.0) { double p = fmod(t / line, 1.0); return p < 0.025 || p >= 0.975 ? 400.0 : -400.0; }
    t -= 30.0;
    if (t < lines * line) { double l = t / line; int y = (int) l; return pattern((int) ((l - y) * WIDTH), y, lines) * 800.0 - 400.0; }
    t -= lines * line;
    if (t < 5.0) return fmod(t * 450.0, 1.0) < 0.5 ? 400.0 : -400.0;
    return -400.0;
}

static double gauss(void)
{
    double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = (rand() + 1.0) / (RAND_MAX + 2.0);
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

static void le32(unsigned char *p, unsigned long v) { p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = (v >> 24) & 255; }

int main(int argc, char **argv)
{
    int lines = argc > 2 ? atoi(argv[2]) : 400;
    double snr = argc > 3 ? atof(argv[3]) : 20.0, err = argc > 4 ? atof(argv[4]) : 0.0;
    double total = 2.0 + 5.0 + 30.0 + lines * 0.5 + 5.0 + 10.0, phase = 0, amp = 0.25, sigma = amp / sqrt(2.0) / pow(10.0, snr / 20.0);
    long n = (long) (total * RATE), i;
    unsigned char h[44];
    FILE *f;
    if (argc < 2 || lines < 10 || lines > 3000) { fprintf(stderr, "use: %s out.wav [lines] [snr dB] [receiver error Hz]\n", argv[0]); return 1; }
    f = fopen(argv[1], "wb");
    if (f == NULL) { perror(argv[1]); return 1; }
    memcpy(h, "RIFF", 4); le32(h + 4, 36 + (unsigned long) n * 4); memcpy(h + 8, "WAVEfmt ", 8); le32(h + 16, 16);
    h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0; le32(h + 24, RATE); le32(h + 28, RATE * 4); h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); le32(h + 40, (unsigned long) n * 4);
    fwrite(h, 1, 44, f);
    srand(1);
    for (i = 0; i < n; i++) {
        double fr = 20000.0 + err + hz_at((double) i / RATE, lines);
        short s[2];
        phase += 2.0 * M_PI * fr / RATE;
        if (phase > M_PI) phase -= 2.0 * M_PI;
        s[0] = (short) ((amp * cos(phase) + sigma * gauss()) * 32767.0);
        s[1] = (short) ((amp * sin(phase) + sigma * gauss()) * 32767.0);
        fwrite(s, 2, 2, f);
    }
    fclose(f);
    printf("%s: %.0f s, %d lines, %.0f dB, receiver %.0f Hz off\n", argv[1], total, lines, snr, err);
    return 0;
}
