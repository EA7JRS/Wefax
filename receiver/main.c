/*
 * Wefax receiver: weather facsimile (radiofax) from HF with an SDRplay or an RTL-SDR.
 *
 * It follows the schedule of the stations chosen in the web interface: before each broadcast it
 * tunes the receiver, the decoder finds the start tone and the phasing, and the chart is stored
 * as a PNG file. It can also stay on one frequency, or decode a recording.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "channel.h"
#include "iq_source.h"
#include "navtex_share.h"
#include "png_out.h"
#include "schedule.h"
#include "store.h"
#include "wefax_decoder.h"

#ifndef STATIONS_FILE
#define STATIONS_FILE "/opt/Wefax/build/estaciones.json"
#endif

#define MIN_LINES 60                  /* fewer lines than this is not a chart: a false start */
#define MAX_LINES 4000
#define TUNE_AHEAD_S 60               /* the receiver is on the frequency this long before a broadcast */
#define LIVE_EVERY_S 20               /* the chart in progress is written this often, for the web interface */

static int service_mode, verbose = 1, quiet;
static const char *source_name = NULL;
static const char *out_dir = NULL;    /* -o: write the charts here and do not touch the database */
static double fixed_khz;              /* -k */
static int force_now;                 /* -m: take lines at once, without waiting for a start tone */
static int opt_lpm = 120, opt_ioc = 576;
static double channel_offset = IQ_TUNE_BELOW_HZ;   /* -O: where the channel is in a recording */
static char rtl_serial_buf[64], sdr_serial_buf[64];

static struct channel *chan;
static struct wefax_decoder *dec;

/* the broadcast the receiver is on (service mode) */
static struct broadcast now_bc;
static int have_bc;
static double tuned_khz;
static long settings_seen;
static time_t leave_at;               /* when to stop listening to this frequency (0: never) */

/* the chart being received */
static unsigned char *px;
static int px_lines, px_cap, px_width, px_ioc, px_lpm, px_phased;
static double px_ppm;
static time_t px_began, px_live;
static int charts_done;

static void say(const char *what)
{
    if (!quiet) { printf("%s\n", what); fflush(stdout); }
    if (service_mode) store_status("state", what);
}

/* ---- the chart */

static void on_begin(void *u, int ioc, int lpm, int width, double ppm, int phased)
{
    char msg[160];
    (void) u;
    px_lines = 0; px_width = width; px_ioc = ioc; px_lpm = lpm; px_ppm = ppm; px_phased = phased;
    px_began = time(NULL); px_live = px_began;
    snprintf(msg, sizeof(msg), "chart begins: IOC %d, %d lines a minute, %s (clock %+.0f ppm, receiver %+.0f Hz off)", ioc, lpm,
             phased ? "phased" : "not phased: it may come out shifted", ppm, wefax_offset_hz(dec));
    printf("%s\n", msg); fflush(stdout);
    if (service_mode) { store_status("state", "receiving a chart"); store_status_num("offset_hz", wefax_offset_hz(dec)); }
}

static void on_line(void *u, const unsigned char *p, int width)
{
    (void) u;
    if (px_lines >= MAX_LINES) return;
    if (px_lines == px_cap) {
        int cap = px_cap ? px_cap * 2 : 512;
        unsigned char *n = realloc(px, (size_t) cap * WEFAX_MAX_WIDTH);
        if (n == NULL) return;
        px = n; px_cap = cap;
    }
    memcpy(px + (size_t) px_lines * WEFAX_MAX_WIDTH, p, (size_t) width);
    px_lines++;
}

static void write_live(void)
{
    char path[700];
    if (!service_mode || px_lines < 10) return;
    snprintf(path, sizeof(path), "%s/charts/live.png", store_dir());
    png_write_grey(path, px, px_width, px_lines, WEFAX_MAX_WIDTH, "chart in progress");
    store_status_num("lines", px_lines);
}

static void on_end(void *u, enum wefax_end why, int drop)
{
    const char *ended = why == WEFAX_END_STOP_TONE ? "stop" : why == WEFAX_END_TOO_LONG ? "long" : why == WEFAX_END_SIGNAL_LOST ? "lost" : "cut";
    int height = px_lines - drop;
    char rel[160], path[900], comment[800], dir[700];
    struct tm tm;
    (void) u;
    if (service_mode) { store_status_num("lines", 0); store_status("state", "listening"); }
    if (height < MIN_LINES) {
        printf("chart dropped: only %d lines (%s)\n", height < 0 ? 0 : height, ended); fflush(stdout);
        return;
    }
    gmtime_r(&px_began, &tm);
    snprintf(comment, sizeof(comment), "Weather facsimile received %04d-%02d-%02d %02d:%02d UTC%s%s on %.1f kHz. IOC %d, %d LPM. %s",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, have_bc ? " from " : "", have_bc ? now_bc.name : "",
             tuned_khz, px_ioc, px_lpm, have_bc ? now_bc.title : "");
    if (out_dir != NULL) {
        snprintf(path, sizeof(path), "%s/wefax_%04d%02d%02d_%02d%02d%02d_%d.png", out_dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec, ++charts_done);
        if (png_write_grey(path, px, px_width, height, WEFAX_MAX_WIDTH, comment) == 0)
            printf("chart written: %s (%d x %d, ended: %s)\n", path, px_width, height, ended);
        else
            printf("cannot write %s\n", path);
        fflush(stdout);
        return;
    }
    snprintf(dir, sizeof(dir), "%s/charts/%04d%02d%02d", store_dir(), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    mkdir(dir, 0775);
    snprintf(rel, sizeof(rel), "%04d%02d%02d/%02d%02d%02d_%s_%.0f.png", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
             have_bc ? now_bc.station : "X", tuned_khz * 10.0);
    snprintf(path, sizeof(path), "%s/charts/%s", store_dir(), rel);
    if (png_write_grey(path, px, px_width, height, WEFAX_MAX_WIDTH, comment) == 0) {
        struct chart_info c;
        /* the chart belongs to the broadcast only if it began in its time */
        int in_time = have_bc && px_began >= now_bc.start - 120 && px_began < now_bc.end;
        memset(&c, 0, sizeof(c));
        c.received = px_began; c.station = have_bc ? now_bc.station : ""; c.title = in_time ? now_bc.title : "";
        c.khz = tuned_khz; c.ioc = px_ioc; c.lpm = px_lpm; c.width = px_width; c.height = height; c.ended = ended;
        c.offset_hz = wefax_offset_hz(dec); c.clock_ppm = px_ppm; c.phased = px_phased; c.file = rel;
        printf("chart stored: %s (%d x %d, ended: %s), id %ld\n", rel, px_width, height, ended, store_add_chart(&c));
    } else
        printf("cannot write %s: %s\n", path, strerror(errno));
    fflush(stdout);
}

/* ---- samples in */

void iq_samples(const short *iq, unsigned int pairs)
{
    static float hz[25200 / 21 + 8];
    static double level;
    unsigned int done = 0;
    while (done < pairs) {
        unsigned int n = pairs - done > 25200 ? 25200 : pairs - done;
        size_t m = channel_feed(chan, iq + 2 * done, n, hz, &level);
        wefax_feed(dec, hz, m);
        done += n;
    }
    if (service_mode) {
        static time_t last;
        if (time(NULL) - last >= 10) {
            double a, b, c;
            last = time(NULL);
            wefax_tones(dec, &a, &b, &c);
            store_status_num("level", level);
            store_status_num("alive", (double) last);
            store_status("decoder", wefax_state(dec) == WEFAX_IMAGE ? "chart" : wefax_state(dec) == WEFAX_PHASING ? "phasing" :
                                    wefax_state(dec) == WEFAX_START ? "start tone" : "waiting for a start tone");
        }
    }
}

void iq_tick(void)
{
    time_t now = time(NULL);
    if (wefax_state(dec) == WEFAX_IMAGE && now - px_live >= LIVE_EVERY_S) { px_live = now; write_live(); }
    if (!service_mode) return;
    /* the broadcast time is over: whatever is being received is ended, unless a chart that began
       in time is still coming (some run a little over) */
    if (leave_at != 0 && now >= leave_at && (wefax_state(dec) != WEFAX_IMAGE || now >= leave_at + 300)) retune_requested = 1;
    if (store_settings_version() != settings_seen) retune_requested = 1;
}

/* ---- running a receiver on one frequency */

static int run_on(double khz, int lpm, int deviation)
{
    int r;
    const char *src = source_name;
    tuned_khz = khz;
    iq_tuned_hz = (long) (khz * 1000.0 + 0.5) - IQ_TUNE_BELOW_HZ;
    channel_free(chan);
    chan = channel_new(channel_offset);
    if (chan == NULL) return 1;
    wefax_set_lpm(dec, lpm);
    wefax_set_deviation(dec, deviation);
    retune_requested = 0;
    device_lost = 0;
    if (force_now) wefax_force_image(dec, opt_ioc, lpm);
    if (src == NULL) {
#ifdef HAVE_LIBSDRPLAY_API
        src = "sdrplay";
#else
        src = "rtlsdr";
#endif
    }
    if (strcmp(src, "file") == 0) r = file_source_run();
    else if (strcmp(src, "rtlsdr") == 0) r = rtlsdr_source_run();
    else r = sdrplay_source_run();
    wefax_flush(dec);                  /* a chart half received is kept as far as it got */
    return r;
}

static void wait_seconds(int s)
{
    while (s-- > 0 && !stop_requested) sleep(1);
}

/* The receiver's settings from the web interface. Returns 0 when it is switched off. */
static int read_settings(void)
{
    char v[64];
    settings_seen = store_settings_version();
    store_get("rx.enabled", v, sizeof(v), "0");
    if (strcmp(v, "1") != 0) return 0;
    store_get("rx.source", v, sizeof(v), "");
    source_name = strcmp(v, "rtlsdr") == 0 ? "rtlsdr" : strcmp(v, "sdrplay") == 0 ? "sdrplay" : NULL;
    store_get("rx.port", v, sizeof(v), "A"); iq_opt.antenna_port = v[0];
    store_get("rx.bias", v, sizeof(v), "0"); iq_opt.bias_t = v[0] == '1';
    store_get("rx.serial", v, sizeof(v), "");
    snprintf(rtl_serial_buf, sizeof(rtl_serial_buf), "%s", v); snprintf(sdr_serial_buf, sizeof(sdr_serial_buf), "%s", v);
    iq_opt.rtl_serial = v[0] != '\0' ? rtl_serial_buf : NULL;
    iq_opt.sdrplay_serial = v[0] != '\0' ? sdr_serial_buf : NULL;
    store_get("rx.direct", v, sizeof(v), "a"); iq_opt.rtl_direct_sampling = v[0] == 'a' ? -1 : v[0] - '0';
    store_get("rx.upconv", v, sizeof(v), "0"); iq_opt.rtl_upconverter_hz = atol(v);
    store_get("rx.gain", v, sizeof(v), "auto"); iq_opt.rtl_gain_tenths = strcmp(v, "auto") == 0 ? -1 : (int) (atof(v) * 10.0 + 0.5);
    store_get("rx.ppm", v, sizeof(v), "0"); iq_opt.rtl_ppm = atoi(v);
    return 1;
}

static void describe_next(const struct broadcast *b, double khz)
{
    char t[40]; struct tm tm;
    gmtime_r(&b->start, &tm);
    strftime(t, sizeof(t), "%Y-%m-%dT%H:%M:%SZ", &tm);
    store_status("next_start", t);
    store_status("next_station", b->station);
    store_status("next_title", b->title);
    store_status_num("next_khz", khz);
}

static int service(void)
{
    int shared_taken = 0;                 /* 1 while Navtex has been asked to leave the receiver */
    char share_url[100] = "";
    int share_rx = 1;

    if (store_init() != 0) { printf("cannot open the database in %s\n", store_dir()); return 1; }
    if (schedule_load(STATIONS_FILE) < 0) printf("no schedule: %s cannot be read\n", STATIONS_FILE);
    store_status_num("started", (double) time(NULL));

    while (!stop_requested) {
        char follow[420], skip[4100], v[100], key[40];
        struct broadcast b;
        time_t now = time(NULL);
        double khz;
        int share;

        if (!read_settings()) {
            if (shared_taken) { navtex_set_receiver(share_url, share_rx, 1); shared_taken = 0; }
            say("off"); have_bc = 0;
            while (!stop_requested && store_settings_version() == settings_seen) sleep(2);
            continue;
        }
        store_get("share.on", v, sizeof(v), "0"); share = v[0] == '1';
        store_get("share.url", share_url, sizeof(share_url), "");
        store_get("share.rx", v, sizeof(v), "1"); share_rx = atoi(v) == 2 ? 2 : 1;
        if (share_url[0] == '\0') share = 0;
        store_purge();

        /* one frequency all the time, instead of the schedule */
        store_get("rx.fixed_khz", v, sizeof(v), "0");
        if (atof(v) > 0) {
            char l[8];
            store_get("rx.fixed_lpm", l, sizeof(l), "120");
            have_bc = 0; leave_at = 0;
            if (share && !shared_taken) { if (navtex_set_receiver(share_url, share_rx, 0) == 0) { shared_taken = 1; wait_seconds(8); } }
            say("listening on a fixed frequency");
            store_status_num("tuned_khz", atof(v));
            if (run_on(atof(v), atoi(l), 400) != 0 && !stop_requested) { say("no receiver"); wait_seconds(15); }
            continue;
        }

        store_get("follow", follow, sizeof(follow), "");
        store_get("skip", skip, sizeof(skip), "");
        if (!schedule_next(now, follow, skip, &b)) {
            if (shared_taken) { navtex_set_receiver(share_url, share_rx, 1); shared_taken = 0; }
            say(follow[0] == '\0' ? "no station followed" : "nothing in the schedule for the stations followed");
            have_bc = 0;
            wait_seconds(30);
            continue;
        }
        snprintf(key, sizeof(key), "freq.%s", b.station);
        store_get(key, v, sizeof(v), "auto");
        khz = strcmp(v, "auto") == 0 || atof(v) <= 0 ? b.suggested_khz : atof(v);
        describe_next(&b, khz);

        /* a shared receiver stays with Navtex until a minute before the broadcast */
        if (share && now < b.start - TUNE_AHEAD_S) {
            if (shared_taken) { navtex_set_receiver(share_url, share_rx, 1); shared_taken = 0; }
            say("waiting: the receiver is with Navtex");
            have_bc = 0;
            while (!stop_requested && time(NULL) < b.start - TUNE_AHEAD_S && store_settings_version() == settings_seen) sleep(2);
            continue;
        }
        if (share && !shared_taken) {
            if (navtex_set_receiver(share_url, share_rx, 0) == 0) { shared_taken = 1; wait_seconds(8); }   /* Navtex needs a moment to let go */
            else { say("Navtex does not answer: cannot take the shared receiver"); wait_seconds(20); continue; }
        }

        now_bc = b; have_bc = 1;
        leave_at = b.end;
        store_status("station", b.station);
        store_status("title", b.title);
        store_status_num("tuned_khz", khz);
        say(now < b.start ? "tuned, waiting for the broadcast" : "listening");
        if (run_on(khz, b.lpm, b.deviation) != 0 && !stop_requested) { say("no receiver"); wait_seconds(15); }
        have_bc = 0;
    }
    if (shared_taken) navtex_set_receiver(share_url, share_rx, 1);
    store_status("state", "stopped");
    return 0;
}

static void usage(const char *prog)
{
    printf(
"Usage: %s [options]\n"
"  -n          service: follow the schedule and the settings of the web interface\n"
"  -k kHz      stay on this frequency (the assigned one: the centre of the channel)\n"
"  -s source   sdrplay | rtlsdr | file   (default: sdrplay when built in, else rtlsdr)\n"
"  -l lpm      lines per minute expected: 60, 90, 120 (default) or 240\n"
"  -m          manual: take lines at once, without waiting for a start tone; with -I 288 for IOC 288\n"
"  -o dir      write the charts in this directory instead of the database (default with -k and -f: .)\n"
"  -b          bias T on: power an active antenna through the coax\n"
"  -q          only say what matters\n"
"SDRplay:\n"
"  -p port     antenna port A, B or C (RSPdx, RSPdx-R2)\n"
"  -S serial   pick the receiver by its serial number\n"
"RTL-SDR:\n"
"  -i index    device index (default 0)\n"
"  -S serial   pick the device by its serial number instead\n"
"  -D mode     direct sampling: 0 off, 1 I branch, 2 Q branch (default: from the model)\n"
"  -u hertz    local oscillator of an external upconverter, e.g. 125000000\n"
"  -g gain     tuner gain in dB (default: automatic). Not used in direct sampling.\n"
"  -P ppm      crystal correction\n"
"Recording:\n"
"  -f file     decode a recording: complex samples at 252000 S/s, .wav (16 bit, I and Q) or raw\n"
"              unsigned 8 bit I/Q (as written by rtl_sdr)\n"
"  -O hertz    where the facsimile channel is in the recording, from its centre (default 20000)\n"
"  -t          play the recording at real speed\n", prog);
}

static void on_signal(int sig) { (void) sig; stop_requested = 1; }

int main(int argc, char **argv)
{
    int c;
    struct wefax_events ev = { NULL, on_begin, on_line, on_end };
    const char *serial = NULL;

    setvbuf(stdout, NULL, _IOLBF, 0);
    opterr = 0;
    while ((c = getopt(argc, argv, "nk:s:l:mI:o:bqp:S:i:D:u:g:P:f:O:th")) != -1) {
        switch (c) {
        case 'n': service_mode = 1; break;
        case 'k': fixed_khz = atof(optarg); break;
        case 's': source_name = optarg; break;
        case 'l': opt_lpm = atoi(optarg); break;
        case 'm': force_now = 1; break;
        case 'I': opt_ioc = atoi(optarg) == 288 ? 288 : 576; break;
        case 'o': out_dir = optarg; break;
        case 'b': iq_opt.bias_t = 1; break;
        case 'q': quiet = 1; verbose = 0; break;
        case 'p': iq_opt.antenna_port = optarg[0]; break;
        case 'S': serial = optarg; break;
        case 'i': iq_opt.rtl_index = atoi(optarg); break;
        case 'D': iq_opt.rtl_direct_sampling = atoi(optarg); break;
        case 'u': iq_opt.rtl_upconverter_hz = atol(optarg); break;
        case 'g': iq_opt.rtl_gain_tenths = (int) (atof(optarg) * 10.0 + 0.5); break;
        case 'P': iq_opt.rtl_ppm = atoi(optarg); break;
        case 'f': iq_opt.file_path = optarg; source_name = "file"; break;
        case 'O': channel_offset = atof(optarg); break;
        case 't': iq_opt.file_realtime = 1; break;
        default: usage(argv[0]); return c == 'h' ? 0 : 1;
        }
    }
    (void) verbose;
    if (source_name != NULL && strcmp(source_name, "sdrplay") != 0 && strcmp(source_name, "rtlsdr") != 0 && strcmp(source_name, "file") != 0) { usage(argv[0]); return 1; }
    if (opt_lpm != 60 && opt_lpm != 90 && opt_lpm != 120 && opt_lpm != 240) { printf("-l: 60, 90, 120 or 240\n"); return 1; }
    iq_opt.rtl_serial = serial; iq_opt.sdrplay_serial = serial;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    dec = wefax_new(CHANNEL_OUT_RATE, opt_lpm, &ev);
    if (dec == NULL) { printf("out of memory\n"); return 1; }

    if (service_mode) return service();

    if (iq_opt.file_path == NULL && fixed_khz <= 0) { usage(argv[0]); return 1; }
    if (out_dir == NULL) out_dir = ".";
    if (iq_opt.file_path != NULL) {
        int r = run_on(fixed_khz > 0 ? fixed_khz : 0, opt_lpm, 400);
        printf("%d chart(s) written\n", charts_done);
        return r;
    }
    printf("Listening on %.1f kHz (receiver on %.1f kHz). Ctrl+C to stop.\n", fixed_khz, fixed_khz - IQ_TUNE_BELOW_HZ / 1000.0);
    while (!stop_requested) {
        if (run_on(fixed_khz, opt_lpm, 400) != 0 && !stop_requested) { printf("no receiver: trying again in 15 s\n"); wait_seconds(15); }
    }
    return 0;
}
