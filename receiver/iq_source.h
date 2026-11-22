/*
 * Wefax receiver: where the I/Q samples come from.
 *
 * Every source delivers complex samples at 252 kS/s (a KiwiSDR sends 12 kS/s: its source brings them up). The receiver is tuned 20 kHz below the
 * facsimile channel, so the channel is clear of the receiver's own carrier at zero.
 * The same layout as the Navtex receiver (EA7JRS/Navtex), whose RTL-SDR and recording sources
 * these are.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef IQ_SOURCE_H
#define IQ_SOURCE_H

#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IQ_SAMPLE_RATE 252000      /* samples per second delivered by a source */
#define IQ_TUNE_BELOW_HZ 20000     /* the receiver is tuned this far below the channel */

/* Frequency the receiver must be tuned to, in Hz (the channel minus IQ_TUNE_BELOW_HZ) */
extern long iq_tuned_hz;

/* Options shared by the sources */
struct iq_options {
    int bias_t;               /* -b  power an active antenna through the coax */
    char antenna_port;        /* -p  SDRplay antenna port A/B/C */
    const char *sdrplay_serial; /* -S with -s sdrplay: pick the SDRplay by its serial number */
    int rtl_index;            /* -i  RTL-SDR device index */
    const char *rtl_serial;   /* -S  RTL-SDR serial number (instead of the index) */
    int rtl_direct_sampling;  /* -D  0 off (tuner or built-in upconverter), 1 I branch, 2 Q branch; -1 = from the model and the frequency */
    long rtl_upconverter_hz;  /* -u  LO of an external upconverter, 0 = none */
    int rtl_gain_tenths;      /* -g  tuner gain in tenths of dB, -1 = automatic */
    int rtl_ppm;              /* -P  crystal correction */
    const char *file_path;    /* -f  recording to decode instead of a receiver */
    int file_realtime;        /* -t  play the recording at real speed */
    const char *kiwi_host;    /* -K  KiwiSDR: its name or address */
    int kiwi_port;            /*     and its port (8073) */
    const char *kiwi_password; /* -W  its password, when it has one */
};
extern struct iq_options iq_opt;

/* Set by SIGINT/SIGTERM: stop and release the receiver */
extern volatile sig_atomic_t stop_requested;
/* Set when the receiver has to be tuned elsewhere, or its settings changed: stop and start again */
extern volatile sig_atomic_t retune_requested;
/* Set by a source when its receiver is unplugged or fails */
extern volatile sig_atomic_t device_lost;

/* Ring buffer: sources push, consume_loop() hands the samples on */
int  iq_ring_init(void);
void iq_ring_free(void);
void iq_push_short(const short *xi, const short *xq, unsigned int num_samples);
void iq_push_u8(const unsigned char *iq, unsigned int num_bytes);   /* RTL-SDR: unsigned 8 bit, I then Q */

/* Hands samples on until a stop or a retune is requested or the receiver is lost
   (no samples for a while counts as lost). */
void consume_loop(void);

/* Given by the program: what to do with the samples (pairs I, Q), and a call about once a second
   to look at the clock and the settings (it sets retune_requested when the receiver must move). */
void iq_samples(const short *interleaved_iq, unsigned int num_pairs);
void iq_tick(void);

/* Each source opens its receiver, runs until told to stop and closes it.
   Returns 0 after a requested stop or retune (or the end of a file), 1 on failure. */
int sdrplay_source_run(void);
int rtlsdr_source_run(void);
int file_source_run(void);
int kiwi_source_run(void);

/* What the KiwiSDR told: where it is (when kiwi_has_position) and the signal level it measures */
extern double kiwi_lat, kiwi_lon, kiwi_rssi_dbm;
extern volatile sig_atomic_t kiwi_has_position;

#ifdef __cplusplus
}
#endif
#endif
