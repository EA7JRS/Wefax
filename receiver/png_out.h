/*
 * Wefax · writes a grey image as a PNG file (with zlib)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef WEFAX_PNG_OUT_H
#define WEFAX_PNG_OUT_H

/* pixels: height rows of width bytes, 0 black to 255 white, each row `stride` bytes after the
   previous one. comment (may be NULL) goes in a text chunk. Returns 0 when the file is written. */
int png_write_grey(const char *path, const unsigned char *pixels, int width, int height, int stride, const char *comment);

#endif
