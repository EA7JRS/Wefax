/*
 * Wefax receiver: the ring buffer between a receiver and the decoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "iq_source.h"

#define RING_SECONDS 8
#define STREAM_TIMEOUT_S 15         /* seconds without samples before the receiver is taken for lost */

struct iq_options iq_opt = { 0, 'A', NULL, 0, NULL, -1, 0, -1, 0, NULL, 0, NULL, 8073, NULL, NULL, 16, 20, 0 };
long iq_tuned_hz = 0;
volatile sig_atomic_t stop_requested = 0;
volatile sig_atomic_t retune_requested = 0;
volatile sig_atomic_t device_lost = 0;

static short *ring;                 /* pairs I, Q */
static size_t ring_pairs;           /* capacity, in pairs */
static size_t head, tail;           /* next pair to write, next pair to read */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int iq_ring_init(void)
{
    ring_pairs = (size_t) IQ_SAMPLE_RATE * RING_SECONDS;
    ring = malloc(ring_pairs * 2 * sizeof(short));
    head = tail = 0;
    return ring != NULL ? 0 : -1;
}

void iq_ring_free(void)
{
    pthread_mutex_lock(&lock);
    free(ring);
    ring = NULL;
    pthread_mutex_unlock(&lock);
}

void iq_push_short(const short *xi, const short *xq, unsigned int n)
{
    unsigned int i;
    pthread_mutex_lock(&lock);
    if (ring != NULL)
        for (i = 0; i < n; i++) {
            ring[2 * head] = xi[i];
            ring[2 * head + 1] = xq[i];
            head = (head + 1) % ring_pairs;
            if (head == tail) tail = (tail + 1) % ring_pairs;      /* full: the oldest sample is lost */
        }
    pthread_mutex_unlock(&lock);
}

/* RTL-SDR samples: unsigned 8 bit, I then Q, scaled to 16 bit */
void iq_push_u8(const unsigned char *iq, unsigned int num_bytes)
{
    short bi[4096], bq[4096];
    unsigned int pairs = num_bytes / 2, done = 0;
    while (done < pairs) {
        unsigned int n = pairs - done, i;
        if (n > 4096) n = 4096;
        for (i = 0; i < n; i++) {
            bi[i] = (short) ((iq[2 * (done + i)] * 2 - 255) * 64);
            bq[i] = (short) ((iq[2 * (done + i) + 1] * 2 - 255) * 64);
        }
        iq_push_short(bi, bq, n);
        done += n;
    }
}

void consume_loop(void)
{
    static short chunk[2 * 25200];
    time_t last_sample = time(NULL), last_tick = 0;

    while (!stop_requested && !device_lost && !retune_requested) {
        size_t n = 0;
        pthread_mutex_lock(&lock);
        while (tail != head && n < 25200) {
            chunk[2 * n] = ring[2 * tail];
            chunk[2 * n + 1] = ring[2 * tail + 1];
            tail = (tail + 1) % ring_pairs;
            n++;
        }
        pthread_mutex_unlock(&lock);
        if (n > 0) {
            iq_samples(chunk, (unsigned int) n);
            last_sample = time(NULL);
        } else {
            usleep(20000);
            if (time(NULL) - last_sample > STREAM_TIMEOUT_S) {
                printf("No samples from the receiver for %d s - giving up\n", STREAM_TIMEOUT_S);
                device_lost = 1;
            }
        }
        if (time(NULL) != last_tick) { last_tick = time(NULL); iq_tick(); }
    }
}
