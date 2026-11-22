/*
 * Wefax · writes a grey image as a PNG file (see png_out.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "png_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void be32(unsigned char *p, unsigned long v) { p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16); p[2] = (unsigned char) (v >> 8); p[3] = (unsigned char) v; }

static int chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
    unsigned char head[8], tail[4];
    unsigned long crc = crc32(0L, (const unsigned char *) type, 4);
    if (n > 0) crc = crc32(crc, data, (uInt) n);
    be32(head, (unsigned long) n); memcpy(head + 4, type, 4);
    be32(tail, crc);
    return fwrite(head, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(tail, 1, 4, f) == 4 ? 0 : -1;
}

int png_write_grey(const char *path, const unsigned char *pixels, int width, int height, int stride, const char *comment)
{
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    unsigned char ihdr[13];
    unsigned char *raw, *packed;
    uLongf packed_len;
    size_t raw_len;
    FILE *f;
    int y, ok = -1;
    char tmp[1024];

    if (width <= 0 || height <= 0 || strlen(path) > sizeof(tmp) - 8) return -1;
    raw_len = (size_t) height * ((size_t) width + 1);
    raw = malloc(raw_len);
    packed_len = compressBound((uLong) raw_len);
    packed = malloc(packed_len);
    if (raw == NULL || packed == NULL) { free(raw); free(packed); return -1; }
    for (y = 0; y < height; y++) {
        raw[(size_t) y * (width + 1)] = 0;                            /* filter: none */
        memcpy(raw + (size_t) y * (width + 1) + 1, pixels + (size_t) y * stride, (size_t) width);
    }
    if (compress2(packed, &packed_len, raw, (uLong) raw_len, 6) != Z_OK) { free(raw); free(packed); return -1; }

    /* written under another name and renamed: nobody sees half a file */
    snprintf(tmp, sizeof(tmp), "%s.part", path);
    f = fopen(tmp, "wb");
    if (f != NULL) {
        be32(ihdr, (unsigned long) width); be32(ihdr + 4, (unsigned long) height);
        ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   /* 8 bit grey */
        ok = fwrite(sig, 1, 8, f) == 8 ? 0 : -1;
        if (ok == 0) ok = chunk(f, "IHDR", ihdr, 13);
        if (ok == 0 && comment != NULL && comment[0] != '\0') {
            size_t n = strlen(comment);
            unsigned char *t = malloc(n + 8);
            if (t != NULL) { memcpy(t, "Comment", 8); memcpy(t + 8, comment, n); ok = chunk(f, "tEXt", t, n + 8); free(t); }
        }
        if (ok == 0) ok = chunk(f, "IDAT", packed, packed_len);
        if (ok == 0) ok = chunk(f, "IEND", NULL, 0);
        if (fclose(f) != 0) ok = -1;
        if (ok == 0 && rename(tmp, path) != 0) ok = -1;
        if (ok != 0) remove(tmp);
    }
    free(raw); free(packed);
    return ok;
}
