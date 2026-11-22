/*
 * Wefax · radiofacsimile decoder (see wefax_decoder.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "wefax_decoder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TONE_WINDOW_S 0.5             /* the tones are looked for in windows this long */
#define TONE_THRESHOLD 0.35           /* share of the window's power that has to be in the tone */
#define TONE_WINDOWS 3                /* consecutive windows: 1.5 s of a 5 s tone */
#define PHASING_BINS 400              /* resolution of the search for the phasing pulse: 0.25 % of a line */
#define PHASING_MIN_LINES 6           /* good phasing lines before the start of a line is believed */
#define PHASING_TIMEOUT_S 50.0        /* after the start tone: 30 s of phasing, with margin */
#define MAX_PHASING_LINES 200
#define MAX_CHART_S (30.0 * 60.0)     /* no chart lasts half an hour */
#define LOST_LINES 40                 /* lines of pure noise in a row that end a chart */

static const int LPMS[] = { 120, 60, 90, 240 };
#define N_LPMS ((int) (sizeof(LPMS) / sizeof(LPMS[0])))

struct goertzel { double coeff, s1, s2; };

struct wefax_decoder {
    double fs;
    struct wefax_events ev;
    enum wefax_state state;
    int lpm_hint;

    /* tones: Goertzel on the pixel value, half a second at a time */
    struct goertzel g[3];             /* 300, 675, 450 Hz */
    double tone_sum, tone_sum2;
    int tone_n, tone_len;
    double tone_level[3];
    int tone_run[3];
    double offset_hz;                 /* how far the receiver is from the centre of the channel, from the start tone */
    double offset_sum; int offset_n;
    double deviation;                 /* Hz from the centre to black or white */

    /* phasing */
    int ioc;
    double since_start;               /* seconds since the start tone ended */
    float *ph;                        /* the pixel values since the phasing began */
    size_t ph_n, ph_cap;
    int saw_start;

    /* the chart */
    int lpm, width;
    double line_len;                  /* samples in a line, as measured */
    double next_line;                 /* sample count (from image_t0) where the next line begins */
    double image_samples;             /* samples taken since the chart began */
    double *acc;                      /* sums for each point of the line in progress */
    int *acc_n;
    unsigned char *row;
    int lines, noisy_lines, stop_lines;
    double line_pos;                  /* samples into the current line */
    double row_sum, row_sum2; int row_n;
};

static void goertzel_init(struct goertzel *g, double f, double fs)
{
    g->coeff = 2.0 * cos(2.0 * M_PI * f / fs);
    g->s1 = g->s2 = 0;
}

struct wefax_decoder *wefax_new(double sample_rate, int lpm, const struct wefax_events *events)
{
    struct wefax_decoder *d = calloc(1, sizeof(*d));
    if (d == NULL) return NULL;
    d->fs = sample_rate;
    if (events != NULL) d->ev = *events;
    d->lpm_hint = lpm > 0 ? lpm : 120;
    d->deviation = WEFAX_DEVIATION_HZ;
    goertzel_init(&d->g[0], WEFAX_START_576_HZ, sample_rate);
    goertzel_init(&d->g[1], WEFAX_START_288_HZ, sample_rate);
    goertzel_init(&d->g[2], WEFAX_STOP_HZ, sample_rate);
    d->tone_len = (int) (sample_rate * TONE_WINDOW_S);
    d->acc = calloc(WEFAX_MAX_WIDTH, sizeof(double));
    d->acc_n = calloc(WEFAX_MAX_WIDTH, sizeof(int));
    d->row = calloc(WEFAX_MAX_WIDTH, 1);
    if (d->acc == NULL || d->acc_n == NULL || d->row == NULL) { wefax_free(d); return NULL; }
    return d;
}

void wefax_free(struct wefax_decoder *d)
{
    if (d == NULL) return;
    free(d->ph); free(d->acc); free(d->acc_n); free(d->row);
    free(d);
}

enum wefax_state wefax_state(const struct wefax_decoder *d) { return d->state; }

void wefax_set_deviation(struct wefax_decoder *d, double hz) { if (hz >= 200.0 && hz <= 600.0) d->deviation = hz; }
void wefax_set_lpm(struct wefax_decoder *d, int lpm) { if (lpm > 0) d->lpm_hint = lpm; }
int wefax_lines(const struct wefax_decoder *d) { return d->state == WEFAX_IMAGE ? d->lines : 0; }
double wefax_offset_hz(const struct wefax_decoder *d) { return d->offset_hz; }
void wefax_set_offset_hz(struct wefax_decoder *d, double hz) { d->offset_hz = hz; }

void wefax_tones(const struct wefax_decoder *d, double *a, double *b, double *c)
{
    if (a) *a = d->tone_level[0];
    if (b) *b = d->tone_level[1];
    if (c) *c = d->tone_level[2];
}

/* 0 black to 1 white */
static double value_of(const struct wefax_decoder *d, double freq_hz)
{
    double v = (freq_hz + d->deviation) / (2.0 * d->deviation);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static void begin_image(struct wefax_decoder *d, int lpm, double line_len, double first_line_in, double ppm, int phased)
{
    d->state = WEFAX_IMAGE;
    d->lpm = lpm;
    d->width = (int) (d->ioc * M_PI + 0.5);
    if (d->width > WEFAX_MAX_WIDTH) d->width = WEFAX_MAX_WIDTH;
    d->line_len = line_len;
    d->line_pos = -first_line_in;         /* negative: samples to wait until the first line begins */
    d->image_samples = 0;
    d->lines = d->noisy_lines = d->stop_lines = 0;
    memset(d->acc, 0, sizeof(double) * WEFAX_MAX_WIDTH);
    memset(d->acc_n, 0, sizeof(int) * WEFAX_MAX_WIDTH);
    d->row_sum = d->row_sum2 = 0; d->row_n = 0;
    free(d->ph); d->ph = NULL; d->ph_n = d->ph_cap = 0;
    if (d->ev.begin) d->ev.begin(d->ev.user, d->ioc, lpm, d->width, ppm, phased);
}

static void end_image(struct wefax_decoder *d, enum wefax_end why)
{
    int drop = 0;
    if (d->state != WEFAX_IMAGE) return;
    /* the lines taken while the stop tone was being recognised are not the chart */
    if (why == WEFAX_END_STOP_TONE) drop = (int) (TONE_WINDOWS * TONE_WINDOW_S * d->lpm / 60.0 + 1.5);
    if (why == WEFAX_END_SIGNAL_LOST) drop = d->noisy_lines;
    if (drop > d->lines) drop = d->lines;
    d->state = WEFAX_IDLE;
    d->saw_start = 0;
    memset(d->tone_run, 0, sizeof(d->tone_run));
    if (d->ev.end) d->ev.end(d->ev.user, why, drop);
}

void wefax_flush(struct wefax_decoder *d)
{
    if (d->state == WEFAX_IMAGE) end_image(d, WEFAX_END_FLUSHED);
    d->state = WEFAX_IDLE;
    d->saw_start = 0;
    free(d->ph); d->ph = NULL; d->ph_n = d->ph_cap = 0;
}

void wefax_force_image(struct wefax_decoder *d, int ioc, int lpm)
{
    wefax_flush(d);
    d->ioc = ioc == 288 ? 288 : 576;
    if (lpm <= 0) lpm = d->lpm_hint;
    begin_image(d, lpm, d->fs * 60.0 / lpm, 0, 0, 0);
}

/*
 * Phasing. The values since the start tone are folded on the length of a line; where the white
 * pulse piles up is where the lines begin. Done for each line rate, and the one whose pulse is
 * 5 % of the line wins. Then the pulse is located in every line on its own, and a straight line
 * through those positions gives the real length of a line (the receiver's clock error).
 *
 * Returns 1 and fills the results when the phasing is believed.
 */
static int find_phasing(const struct wefax_decoder *d, int *lpm_out, double *line_len_out, double *origin_out, int *good_lines_out)
{
    static double fold[PHASING_BINS];
    static int cnt[PHASING_BINS];
    int best_lpm = 0, k, i;
    double best_score = 0, best_centre = 0;

    for (k = 0; k < N_LPMS; k++) {
        double len = d->fs * 60.0 / LPMS[k];
        double total = 0, in_best = -1, width;
        int w = PHASING_BINS / 20, at = 0, white = 0;      /* 5 % of the line */
        size_t s;
        if (d->ph_n < (size_t) (len * PHASING_MIN_LINES)) continue;
        memset(fold, 0, sizeof(fold)); memset(cnt, 0, sizeof(cnt));
        for (s = 0; s < d->ph_n; s++) {
            int b = (int) (fmod((double) s, len) / len * PHASING_BINS);
            if (b >= PHASING_BINS) b = PHASING_BINS - 1;
            fold[b] += d->ph[s]; cnt[b]++;
        }
        for (i = 0; i < PHASING_BINS; i++) { if (cnt[i]) fold[i] /= cnt[i]; total += fold[i]; if (fold[i] > 0.5) white++; }
        for (i = 0; i < PHASING_BINS; i++) {
            double in = 0; int j;
            for (j = 0; j < w; j++) in += fold[(i + j) % PHASING_BINS];
            if (in > in_best) { in_best = in; at = i; }
        }
        width = (double) white / PHASING_BINS;
        {
            double mean_in = in_best / w, mean_out = (total - in_best) / (PHASING_BINS - w);
            /* a pulse twice as wide, or two pulses, mean another line rate */
            double score = (mean_in - mean_out) - 4.0 * fabs(width - 0.05);
            if (score > best_score) { best_score = score; best_lpm = LPMS[k]; best_centre = (at + w / 2.0) / PHASING_BINS * len; }
        }
    }
    if (best_lpm == 0 || best_score < 0.5) return 0;

    /* the pulse in every line: centre of mass of the white around where it is expected */
    {
        double len = d->fs * 60.0 / best_lpm;
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0, lines = (int) (d->ph_n / len), l;
        double half = len * 0.06;
        static double px[MAX_PHASING_LINES], py[MAX_PHASING_LINES];
        if (lines > MAX_PHASING_LINES) lines = MAX_PHASING_LINES;
        for (l = 0; l < lines; l++) {
            double c = l * len + best_centre, m = 0, mw = 0, rest = 0; int nrest = 0;
            long a = (long) (c - half), b = (long) (c + half), s;
            long la = (long) (c - len / 2), lb = (long) (c + len / 2);
            if (la < 0 || lb >= (long) d->ph_n) continue;
            for (s = a; s <= b; s++) { m += d->ph[s] * (s - c); mw += d->ph[s]; }
            for (s = la; s < lb; s += 4) if (s < a || s > b) { rest += d->ph[s]; nrest++; }
            /* a phasing line: white where the pulse is, black elsewhere */
            if (mw < 0.03 * len || (nrest > 0 && rest / nrest > 0.2)) continue;
            px[n] = l; py[n] = c + m / mw; n++;
        }
        if (n < PHASING_MIN_LINES) return 0;
        for (l = 0; l < n; l++) { sx += px[l]; sy += py[l]; sxx += px[l] * px[l]; sxy += px[l] * py[l]; }
        {
            double den = n * sxx - sx * sx;
            double slope = den > 0 ? (n * sxy - sx * sy) / den : len;
            double icept = (sy - slope * sx) / n;
            /* a clock more than 0.1 % off is not a clock error: keep the nominal length */
            if (fabs(slope - len) > len * 0.001 || n < 10) { slope = len; icept = (sy - slope * sx) / n; }
            *lpm_out = best_lpm;
            *line_len_out = slope;
            *origin_out = icept;              /* sample (since the phasing began) where line 0 begins */
            *good_lines_out = n;
        }
    }
    return 1;
}

/*
 * Symmetrical phasing: half of each line white, half black, instead of the 5 % pulse. Folded on
 * the right length of a line it is one white run of half the line; on twice that length, two runs;
 * on any other, grey. The line is taken to begin where black turns to white (white first): the
 * text of the recommendation was not at hand to check which edge it is, and no station is known
 * to use it today, so a chart phased this way may come out shifted by half a line.
 */
static int find_sym_phasing(const struct wefax_decoder *d, int *lpm_out, double *line_len_out, double *origin_out, int *good_lines_out)
{
    static double fold[PHASING_BINS];
    static int cnt[PHASING_BINS];
    int k, i, best_lpm = 0;
    double best_edge = 0;

    for (k = 0; k < N_LPMS && best_lpm == 0; k++) {
        double len = d->fs * 60.0 / LPMS[k];
        int white = 0, clear = 0, rises = 0, at = 0;
        size_t s;
        if (d->ph_n < (size_t) (len * PHASING_MIN_LINES)) continue;
        memset(fold, 0, sizeof(fold)); memset(cnt, 0, sizeof(cnt));
        for (s = 0; s < d->ph_n; s++) {
            int b = (int) (fmod((double) s, len) / len * PHASING_BINS);
            if (b >= PHASING_BINS) b = PHASING_BINS - 1;
            fold[b] += d->ph[s]; cnt[b]++;
        }
        for (i = 0; i < PHASING_BINS; i++) {
            if (cnt[i]) fold[i] /= cnt[i];
            if (fold[i] > 0.5) white++;
            if (fold[i] > 0.8 || fold[i] < 0.2) clear++;
        }
        for (i = 0; i < PHASING_BINS; i++)
            if (fold[i] <= 0.5 && fold[(i + 1) % PHASING_BINS] > 0.5) { rises++; at = i + 1; }
        /* one white run of half the line, with sharp edges */
        if (rises == 1 && abs(white - PHASING_BINS / 2) < PHASING_BINS / 20 && clear > PHASING_BINS * 9 / 10) {
            best_lpm = LPMS[k]; best_edge = (double) at / PHASING_BINS * len;
        }
    }
    if (best_lpm == 0) return 0;

    /* the edge in every line: with black before it and white after, the white in a window around
       it tells where it is */
    {
        double len = d->fs * 60.0 / best_lpm;
        double sx = 0, sy = 0, sxx = 0, sxy = 0, half = len * 0.06;
        int n = 0, lines = (int) (d->ph_n / len), l;
        static double px[MAX_PHASING_LINES], py[MAX_PHASING_LINES];
        if (lines > MAX_PHASING_LINES) lines = MAX_PHASING_LINES;
        for (l = 0; l < lines; l++) {
            double c = l * len + best_edge, w = 0, hi = 0, lo = 0; long nh = 0, nl = 0, s;
            long a = (long) (c - half), b = (long) (c + half);
            if (a < 0 || (long) (c + len * 0.94) >= (long) d->ph_n) continue;
            for (s = a; s <= b; s++) w += d->ph[s];
            for (s = (long) (c + len * 0.06); s < (long) (c + len * 0.44); s += 4) { hi += d->ph[s]; nh++; }
            for (s = (long) (c + len * 0.56); s < (long) (c + len * 0.94); s += 4) { lo += d->ph[s]; nl++; }
            if (nh == 0 || nl == 0 || hi / nh < 0.8 || lo / nl > 0.2) continue;
            px[n] = l; py[n] = (double) b + 1 - w; n++;
        }
        if (n < PHASING_MIN_LINES) return 0;
        for (l = 0; l < n; l++) { sx += px[l]; sy += py[l]; sxx += px[l] * px[l]; sxy += px[l] * py[l]; }
        {
            double den = n * sxx - sx * sx;
            double slope = den > 0 ? (n * sxy - sx * sy) / den : len;
            if (fabs(slope - len) > len * 0.001 || n < 10) slope = len;
            *lpm_out = best_lpm;
            *line_len_out = slope;
            *origin_out = (sy - slope * sx) / n;
            *good_lines_out = n;
        }
    }
    return 1;
}

/* Is the line that begins at sample `start` a line of symmetrical phasing? White its first half,
   black the second. */
static int is_sym_phasing_line(const float *ph, size_t ph_n, double start, double line_len)
{
    long s; double hi = 0, lo = 0; long nh = 0, nl = 0;
    if (start < 0 || (long) (start + line_len * 0.94) >= (long) ph_n) return 1;
    for (s = (long) (start + line_len * 0.06); s < (long) (start + line_len * 0.44); s += 2) { hi += ph[s]; nh++; }
    for (s = (long) (start + line_len * 0.56); s < (long) (start + line_len * 0.94); s += 2) { lo += ph[s]; nl++; }
    return nh > 0 && nl > 0 && hi / nh > 0.8 && lo / nl < 0.2;
}

/* Is the line that begins at sample `start` a phasing line? Black, but for the white pulse that
   straddles its beginning and its end. A black line of the chart (a frame) is not one. */
static int is_phasing_line(const float *ph, size_t ph_n, double start, double line_len)
{
    long s, a = (long) (start + line_len * 0.06), b = (long) (start + line_len * 0.94);
    long p0 = (long) (start - line_len * 0.015), p1 = (long) (start + line_len * 0.015);
    double sum = 0, pulse = 0; long n = 0, np = 0;
    if (p0 < 0 || b >= (long) ph_n) return 1;                 /* not all there yet: no reason to say it is not */
    for (s = a; s < b; s += 2) { sum += ph[s]; n++; }
    for (s = p0; s <= p1; s++) { pulse += ph[s]; np++; }
    return n > 0 && sum / n < 0.2 && np > 0 && pulse / np > 0.5;
}

/* Is the last whole line of the phasing buffer still a phasing line? */
static int last_line_is_phasing(const struct wefax_decoder *d, double line_len, double origin, int sym)
{
    double n_lines = floor(((double) d->ph_n - origin) / line_len);
    double start = origin + (n_lines - 1) * line_len;
    return sym ? is_sym_phasing_line(d->ph, d->ph_n, start, line_len) : is_phasing_line(d->ph, d->ph_n, start, line_len);
}

static void tone_window(struct wefax_decoder *d)
{
    double mean = d->tone_sum / d->tone_n;
    double var = d->tone_sum2 / d->tone_n - mean * mean;
    int k;
    for (k = 0; k < 3; k++) {
        struct goertzel *g = &d->g[k];
        double p = g->s1 * g->s1 + g->s2 * g->s2 - g->coeff * g->s1 * g->s2;
        /* power of a square wave's fundamental against the power of everything: 8/pi^2 at most */
        double share = var > 1e-4 ? (2.0 * p / ((double) d->tone_n * d->tone_n)) / var : 0;
        d->tone_level[k] = share > 1 ? 1 : share;
        /* black to white is 800 Hz: a tone has a variance near 0.25 in these units; noise, far more */
        d->tone_run[k] = share > TONE_THRESHOLD && var > 0.05 && var < 1.0 ? d->tone_run[k] + 1 : 0;
        g->s1 = g->s2 = 0;
    }
    /* A start tone spends the same time in black as in white, so its mean frequency is the centre of
       the channel as this receiver sees it: an RTL-SDR 50 parts per million off is 700 Hz away at
       14 MHz, more than the whole swing from black to white. */
    if (d->state != WEFAX_IMAGE && (d->tone_run[0] > 0 || d->tone_run[1] > 0)) {
        if (d->tone_run[0] == 1 || d->tone_run[1] == 1) { d->offset_sum = 0; d->offset_n = 0; }
        d->offset_sum += mean * 2.0 * WEFAX_DEVIATION_HZ; d->offset_n++;
        if (d->tone_run[0] >= TONE_WINDOWS || d->tone_run[1] >= TONE_WINDOWS) d->offset_hz = d->offset_sum / d->offset_n;
    }
    d->tone_sum = d->tone_sum2 = 0; d->tone_n = 0;

    if (d->state != WEFAX_IMAGE && (d->tone_run[0] >= TONE_WINDOWS || d->tone_run[1] >= TONE_WINDOWS)) {
        /* a start tone: the phasing follows when it ends */
        d->ioc = d->tone_run[1] >= TONE_WINDOWS ? 288 : 576;
        d->state = WEFAX_START;
        d->saw_start = 1;
    } else if (d->state == WEFAX_START && d->tone_run[0] == 0 && d->tone_run[1] == 0) {
        d->state = WEFAX_PHASING;
        d->since_start = 0;
        d->ph_n = 0;
    } else if (d->state == WEFAX_IMAGE && d->tone_run[2] >= TONE_WINDOWS) {
        end_image(d, WEFAX_END_STOP_TONE);
    }
}

static void image_sample(struct wefax_decoder *d, double v, double raw_hz);

static void phasing_sample(struct wefax_decoder *d, double v)
{
    if (d->ph_n == d->ph_cap) {
        size_t cap = d->ph_cap ? d->ph_cap * 2 : (size_t) (d->fs * 8);
        float *p = realloc(d->ph, cap * sizeof(float));
        if (p == NULL) { d->state = WEFAX_IDLE; return; }
        d->ph = p; d->ph_cap = cap;
    }
    d->ph[d->ph_n++] = (float) v;
    d->since_start += 1.0 / d->fs;

    /* once a second: is the start of a line known, and has the chart begun? */
    if (d->ph_n % (size_t) d->fs == 0) {
        int lpm = 0, good = 0, sym = 0;
        double len = 0, origin = 0;
        int found = find_phasing(d, &lpm, &len, &origin, &good);
        if (!found) { found = find_sym_phasing(d, &lpm, &len, &origin, &good); sym = found; }
        if (found && !last_line_is_phasing(d, len, origin, sym)) {
            /* The chart began with the first line that was not a phasing line. Go back to it: the
               lines of the chart already received are in the buffer. */
            double nominal = d->fs * 60.0 / lpm;
            double total = floor(((double) d->ph_n - origin) / len);
            long l = (long) total - 1;
            double first, ppm = (len - nominal) / nominal * 1e6;
            float *buf = d->ph; size_t n = d->ph_n, s;
            while (l > 0 && !(sym ? is_sym_phasing_line(buf, n, origin + (l - 1) * len, len) : is_phasing_line(buf, n, origin + (l - 1) * len, len))) l--;   /* line l - 1 is phasing: the chart begins at l */
            first = origin + l * len;
            d->ph = NULL; d->ph_n = d->ph_cap = 0;
            begin_image(d, lpm, len, 0, ppm, sym ? 2 : 1);
            /* replay what was received of the chart */
            for (s = (size_t) (first + 0.5); s < n && d->state == WEFAX_IMAGE; s++)
                image_sample(d, buf[s], buf[s] * 2.0 * d->deviation - d->deviation);
            free(buf);
            return;
        }
        if (d->since_start > PHASING_TIMEOUT_S) {
            /* no phasing found: the chart is taken anyway, shifted, rather than lost */
            int ioc = d->ioc;
            free(d->ph); d->ph = NULL; d->ph_n = d->ph_cap = 0;
            d->ioc = ioc;
            begin_image(d, d->lpm_hint, d->fs * 60.0 / d->lpm_hint, 0, 0, 0);
        }
    }
}

static void image_sample(struct wefax_decoder *d, double v, double raw_hz)
{
    d->image_samples += 1.0;
    if (d->line_pos < 0) { d->line_pos += 1.0; return; }
    {
        int col = (int) (d->line_pos / d->line_len * d->width);
        if (col >= d->width) col = d->width - 1;
        d->acc[col] += v; d->acc_n[col]++;
    }
    /* noise has a frequency all over the place; a chart stays between black and white */
    d->row_sum += fabs(raw_hz) > 3.0 * WEFAX_DEVIATION_HZ ? 1.0 : 0.0; d->row_n++;
    d->line_pos += 1.0;
    if (d->line_pos >= d->line_len) {
        int i; unsigned char last = 0;
        for (i = 0; i < d->width; i++) {
            if (d->acc_n[i]) last = (unsigned char) (d->acc[i] / d->acc_n[i] * 255.0 + 0.5);
            d->row[i] = last; d->acc[i] = 0; d->acc_n[i] = 0;
        }
        d->line_pos -= d->line_len;
        d->lines++;
        d->noisy_lines = d->row_n > 0 && d->row_sum / d->row_n > 0.25 ? d->noisy_lines + 1 : 0;
        d->row_sum = 0; d->row_n = 0;
        if (d->ev.line) d->ev.line(d->ev.user, d->row, d->width);
        if (d->noisy_lines >= LOST_LINES) end_image(d, WEFAX_END_SIGNAL_LOST);
        else if (d->image_samples > MAX_CHART_S * d->fs) end_image(d, WEFAX_END_TOO_LONG);
    }
}

void wefax_feed(struct wefax_decoder *d, const float *freq_hz, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        /* the tones are looked for in the frequency as it comes, wherever the receiver is tuned */
        double raw = freq_hz[i] > 3000.0f ? 3000.0 : freq_hz[i] < -3000.0f ? -3000.0 : freq_hz[i];
        double u = raw / (2.0 * WEFAX_DEVIATION_HZ);
        double hz = freq_hz[i] - d->offset_hz;
        double v = value_of(d, hz);
        int k;
        for (k = 0; k < 3; k++) {
            struct goertzel *g = &d->g[k];
            double s0 = u + g->coeff * g->s1 - g->s2;
            g->s2 = g->s1; g->s1 = s0;
        }
        d->tone_sum += u; d->tone_sum2 += u * u;
        if (++d->tone_n >= d->tone_len) tone_window(d);

        if (d->state == WEFAX_PHASING) phasing_sample(d, v);
        else if (d->state == WEFAX_IMAGE) image_sample(d, v, hz);
    }
}
