/*
 * Wefax receiver: decode a recording instead of a live receiver.
 *
 * The file must hold complex samples at 252 kS/s, with the facsimile channel 20 kHz above the
 * centre (what the receiver writes with -d), or wherever -O says:
 *   - .wav, 16 bit, two channels (I and Q);
 *   - anything else: raw unsigned 8 bit I/Q, what "rtl_sdr -s 252000" writes.
 * From the Navtex receiver (EA7JRS/Navtex).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "iq_source.h"

#define CHUNK_PAIRS 25200   /* a tenth of a second */

/* Skips the wav header up to the sample data. Returns 0 when the format fits. */
static int wav_seek_data(FILE *f)
{
    unsigned char h[12], c[8], fmt[16];
    unsigned long n;
    int have_fmt = 0;

    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0)
        return -1;
    while (fread(c, 1, 8, f) == 8)
    {
        n = c[4] | (c[5] << 8) | (c[6] << 16) | ((unsigned long) c[7] << 24);
        if (memcmp(c, "fmt ", 4) == 0 && n >= 16)
        {
            if (fread(fmt, 1, 16, f) != 16) return -1;
            /* PCM, 2 channels, 16 bits */
            if ((fmt[0] | (fmt[1] << 8)) != 1 || (fmt[2] | (fmt[3] << 8)) != 2 || (fmt[14] | (fmt[15] << 8)) != 16)
                return -2;
            n -= 16;
            have_fmt = 1;
        }
        else if (memcmp(c, "data", 4) == 0)
            return have_fmt ? 0 : -1;
        if (n > 0 && fseek(f, (long) (n + (n & 1)), SEEK_CUR) != 0) return -1;
    }
    return -1;
}

int file_source_run(void)
{
    FILE *f;
    const char *ext;
    int is_wav;
    short *pairs;
    unsigned char *raw;
    size_t got;
    unsigned long long total = 0;
    struct timespec tenth = { 0, 100000000L };

    if (iq_opt.file_path == NULL)
    {
        printf("file: no recording given (-f)\n");
        return 1;
    }
    f = fopen(iq_opt.file_path, "rb");
    if (f == NULL)
    {
        perror(iq_opt.file_path);
        return 1;
    }
    ext = strrchr(iq_opt.file_path, '.');
    is_wav = (ext != NULL && (strcmp(ext, ".wav") == 0 || strcmp(ext, ".WAV") == 0));
    if (is_wav)
    {
        int r = wav_seek_data(f);
        if (r != 0)
        {
            printf(r == -2 ? "file: the wav must be 16 bit PCM with two channels (I and Q)\n" : "file: not a wav file\n");
            fclose(f);
            return 1;
        }
    }
    printf("file: decoding %s (%s), %d S/s\n", iq_opt.file_path,
           is_wav ? "wav, 16 bit I/Q" : "raw unsigned 8 bit I/Q", IQ_SAMPLE_RATE);

    pairs = malloc(CHUNK_PAIRS * 2 * sizeof(short));
    raw = malloc(CHUNK_PAIRS * 2);
    if (pairs == NULL || raw == NULL)
    {
        printf("out of memory\n");
        free(pairs); free(raw); fclose(f);
        return 1;
    }

    while (!stop_requested)
    {
        if (is_wav)
        {
            got = fread(pairs, 2 * sizeof(short), CHUNK_PAIRS, f);
        }
        else
        {
            size_t i;
            got = fread(raw, 2, CHUNK_PAIRS, f);
            for (i = 0; i < got * 2; i++)
                pairs[i] = (short) ((raw[i] * 2 - 255) * 64);
        }
        if (got == 0) break;
        iq_samples(pairs, (unsigned int) got);
        total += got;
        if (iq_opt.file_realtime) nanosleep(&tenth, NULL);
    }

    printf("file: %.1f s of recording decoded\n", (double) total / IQ_SAMPLE_RATE);
    free(pairs); free(raw); fclose(f);
    return 0;
}
