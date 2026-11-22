/*
 * Wefax receiver: the parts of the KiwiSDR protocol that need no network.
 *
 * A KiwiSDR is a short wave receiver on the network. A client opens a WebSocket on it
 * (ws://host:8073/<number>/SND), says who it is and asks for a frequency in "iq" mode; the
 * receiver then sends complex samples at about 12 kS/s (it says the exact rate). The protocol is
 * the one of kiwiclient (github.com/jks-prv/kiwiclient, the reference client), read from its
 * source; nothing of it is copied here.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef KIWI_PROTO_H
#define KIWI_PROTO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- WebSocket (RFC 6455), client side, no TLS */

/* The request that opens the WebSocket. key16: 16 random bytes. Returns the length, 0 if it does not fit. */
size_t kiwi_ws_request(char *out, size_t size, const char *host, int port, unsigned long stream_id, const unsigned char *key16);
/* 1 when the answer of the server (up to the empty line) accepts the WebSocket */
int kiwi_ws_accepted(const char *answer);
/* A frame from the client (masked, as the RFC wants), opcode 1 text, 9 ping, 10 pong, 8 close.
   Returns its length, 0 if it does not fit. */
size_t kiwi_ws_frame(unsigned char *out, size_t size, int opcode, const void *payload, size_t len, const unsigned char *mask4);
/* Looks at the bytes received so far. Returns 1 with a whole frame (its opcode, whether it is the
   last piece, where its payload is and how many bytes the frame takes), 0 when more bytes are
   needed, -1 when this is not a WebSocket frame or it is larger than max_payload. */
int kiwi_ws_parse(const unsigned char *buf, size_t n, size_t max_payload, int *opcode, int *fin, const unsigned char **payload, size_t *payload_len, size_t *frame_len);

/* ---- KiwiSDR messages */

/* The value of a name in the body of a MSG ("audio_rate=12000 sample_rate=12001.135 ..."); a name
   with no value gives "". Returns 1 when the name is there. */
int kiwi_msg_value(const char *body, size_t len, const char *name, char *out, size_t size);
/* The receiver's own position, from the "load_cfg" value (JSON, URL-encoded). Returns 1 with it. */
int kiwi_cfg_position(const char *value, double *lat, double *lon);
/* The commands that start the samples, one per line, for a frequency in kHz. offset_khz: the
   receiver's own frequency offset (a transverter), that it says in "freq_offset". */
size_t kiwi_setup_commands(char *out, size_t size, double khz, double offset_khz);

#define KIWI_SND_STEREO 0x08
#define KIWI_SND_COMPRESSED 0x10
#define KIWI_SND_ADC_OVERLOAD 0x02
/* The complex samples of a SND message body (what follows the three letters "SND"). Writes pairs
   I, Q (at most max_pairs) and returns how many; -1 when the body is not I/Q (mono or compressed
   sound: the receiver is not in "iq" mode). rssi_dbm may be NULL. */
int kiwi_snd_iq(const unsigned char *body, size_t len, short *pairs, int max_pairs, double *rssi_dbm, int *flags);

/* ---- from the receiver's rate to the 252 kS/s of the sources, 20 kHz up */

struct kiwi_resampler;
struct kiwi_resampler *kiwi_resampler_new(double in_rate, double out_rate, double shift_hz);
void kiwi_resampler_free(struct kiwi_resampler *r);
/* Takes pairs I, Q and writes the pairs that come out (at most max_out; with room for
   n * out_rate / in_rate + 2 none is lost). Returns how many. */
size_t kiwi_resample(struct kiwi_resampler *r, const short *in, size_t n, short *out, size_t max_out);

#ifdef __cplusplus
}
#endif
#endif
