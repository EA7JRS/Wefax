/*
 * Wefax · the schedule (see schedule.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "schedule.h"

#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#define MAX_BROADCAST_MIN 30
#define MIN_BROADCAST_MIN 8

static cJSON *root;

static int have_pos;
static double pos_lat, pos_lon;
#define RAD (3.14159265358979323846 / 180.0)

void schedule_set_position(int have, double lat, double lon) { have_pos = have; pos_lat = lat; pos_lon = lon; }

double schedule_sun_elevation(double lat, double lon, time_t when)
{
    double n = (double) when / 86400.0 + 2440587.5 - 2451545.0;
    double L = fmod(280.46 + 0.9856474 * n, 360.0), g = fmod(357.528 + 0.9856003 * n, 360.0) * RAD;
    double lam = (L + 1.915 * sin(g) + 0.02 * sin(2 * g)) * RAD, eps = (23.439 - 0.0000004 * n) * RAD;
    double dec = asin(sin(eps) * sin(lam)), ra = atan2(cos(eps) * sin(lam), cos(lam));
    double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0), ha = (gmst * 15.0 + lon) * RAD - ra;
    return asin(sin(lat * RAD) * sin(dec) + cos(lat * RAD) * cos(dec) * cos(ha)) / RAD;
}

double schedule_distance_km(double lat1, double lon1, double lat2, double lon2)
{
    double a = sin((lat2 - lat1) * RAD / 2), b = sin((lon2 - lon1) * RAD / 2);
    double h = a * a + cos(lat1 * RAD) * cos(lat2 * RAD) * b * b;
    return 2.0 * 6371.0 * asin(sqrt(h > 1 ? 1 : h));
}

double schedule_pick(const double *khz, int n, double lat1, double lon1, double lat2, double lon2, time_t when, char *why)
{
    /* the middle of the great circle, through the sum of the two unit vectors */
    double x = cos(lat1 * RAD) * cos(lon1 * RAD) + cos(lat2 * RAD) * cos(lon2 * RAD);
    double y = cos(lat1 * RAD) * sin(lon1 * RAD) + cos(lat2 * RAD) * sin(lon2 * RAD);
    double z = sin(lat1 * RAD) + sin(lat2 * RAD);
    double mid_lat = atan2(z, sqrt(x * x + y * y)) / RAD, mid_lon = atan2(y, x) / RAD;
    double sun = schedule_sun_elevation(mid_lat, mid_lon, when), km = schedule_distance_km(lat1, lon1, lat2, lon2);
    double sorted[SCHEDULE_MAX_FREQS], best;
    int i, j;
    if (n <= 0) { *why = 'm'; return 0; }
    if (n > SCHEDULE_MAX_FREQS) n = SCHEDULE_MAX_FREQS;
    for (i = 0; i < n; i++) sorted[i] = khz[i];
    for (i = 1; i < n; i++) for (j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) { double t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
    if (sun < -6.0) {
        *why = 'n'; best = sorted[0];
        for (i = 0; i < n; i++) if (sorted[i] >= 3000.0) { best = sorted[i]; break; }
        return best;
    }
    if (sun <= 6.0) { *why = 't'; return sorted[n / 2]; }
    *why = 'd'; best = sorted[n - 1];
    if (km < 3000.0) for (i = n - 1; i >= 0; i--) { best = sorted[i]; if (sorted[i] <= 14500.0) break; }
    return best;
}

static cJSON *prop;
static time_t prop_mtime;

int schedule_load_propagation(const char *path)
{
    struct stat st;
    FILE *f;
    char *text;
    long n;
    if (stat(path, &st) != 0) { cJSON_Delete(prop); prop = NULL; prop_mtime = 0; return 0; }
    if (prop != NULL && st.st_mtime == prop_mtime) return 1;
    f = fopen(path, "rb");
    if (f == NULL) return prop != NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 4 * 1024 * 1024 || (text = malloc((size_t) n + 1)) == NULL) { fclose(f); return prop != NULL; }
    n = (long) fread(text, 1, (size_t) n, f); text[n] = '\0';
    fclose(f);
    cJSON_Delete(prop);
    prop = cJSON_Parse(text);
    prop_mtime = st.st_mtime;
    free(text);
    return prop != NULL;
}

/* The most reliable of the frequencies on the air at that hour, by the table; 0 when it does not say */
static double from_table(const char *id, const double *khz, int n, time_t when, double *reliability)
{
    const cJSON *stations = cJSON_GetObjectItemCaseSensitive(prop, "stations"), *s, *list, *rel, *snr, *y, *m;
    struct tm tm;
    double best = 0, best_rel = -1, best_snr = -1e9;
    int i, j;
    if (prop == NULL) return 0;
    gmtime_r(&when, &tm);
    y = cJSON_GetObjectItemCaseSensitive(prop, "year"); m = cJSON_GetObjectItemCaseSensitive(prop, "month");
    if (!cJSON_IsNumber(y) || !cJSON_IsNumber(m) || y->valueint != tm.tm_year + 1900 || m->valueint != tm.tm_mon + 1) return 0;
    s = cJSON_GetObjectItemCaseSensitive(stations, id);
    list = cJSON_GetObjectItemCaseSensitive(s, "khz"); rel = cJSON_GetObjectItemCaseSensitive(s, "rel"); snr = cJSON_GetObjectItemCaseSensitive(s, "snr");
    for (i = 0; i < n; i++) {
        for (j = 0; j < cJSON_GetArraySize(list); j++) {
            const cJSON *k = cJSON_GetArrayItem(list, j);
            const cJSON *r = cJSON_GetArrayItem(cJSON_GetArrayItem(rel, j), tm.tm_hour), *q = cJSON_GetArrayItem(cJSON_GetArrayItem(snr, j), tm.tm_hour);
            double sn = cJSON_IsNumber(q) ? q->valuedouble : -1e9;
            if (!cJSON_IsNumber(k) || fabs(k->valuedouble - khz[i]) > 0.05 || !cJSON_IsNumber(r)) continue;
            /* reliability first; between equals, the stronger signal */
            if (r->valuedouble > best_rel + 0.005 || (r->valuedouble > best_rel - 0.005 && sn > best_snr)) { best = khz[i]; best_rel = r->valuedouble; best_snr = sn; }
        }
    }
    *reliability = best_rel;
    return best;
}

void schedule_free(void) { cJSON_Delete(root); root = NULL; }

int schedule_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long n;
    cJSON *j, *st;
    if (f == NULL) return -1;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024 || (text = malloc((size_t) n + 1)) == NULL) { fclose(f); return -1; }
    if (fread(text, 1, (size_t) n, f) != (size_t) n) { free(text); fclose(f); return -1; }
    text[n] = '\0';
    fclose(f);
    j = cJSON_Parse(text);
    free(text);
    st = j != NULL ? cJSON_GetObjectItemCaseSensitive(j, "stations") : NULL;
    if (!cJSON_IsArray(st) || cJSON_GetArraySize(st) == 0) { cJSON_Delete(j); return -1; }
    schedule_free();
    root = j;
    return cJSON_GetArraySize(st);
}

/* "0430" -> 270; -1 when it is not a time */
static int minutes_of(const char *hhmm)
{
    int h, m;
    if (hhmm == NULL || strlen(hhmm) != 4 || strspn(hhmm, "0123456789") != 4) return -1;
    h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0'); m = (hhmm[2] - '0') * 10 + (hhmm[3] - '0');
    if (h > 24 || m > 59 || (h == 24 && m != 0)) return -1;
    return h * 60 + m;
}

/* Is `item` one of the comma separated words of `list`? */
static int in_list(const char *list, const char *item)
{
    size_t n = strlen(item);
    const char *p = list;
    if (list == NULL || n == 0) return 0;
    while ((p = strstr(p, item)) != NULL) {
        if ((p == list || p[-1] == ',') && (p[n] == ',' || p[n] == '\0')) return 1;
        p += n;
    }
    return 0;
}

static const char *text_of(const cJSON *o, const char *name)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
    return cJSON_IsString(v) ? v->valuestring : "";
}

int schedule_next(time_t now, const char *follow, const char *skip, struct broadcast *out)
{
    const cJSON *stations = root != NULL ? cJSON_GetObjectItemCaseSensitive(root, "stations") : NULL, *st;
    time_t day = now - now % 86400, best_start = 0;
    int found = 0;

    cJSON_ArrayForEach(st, stations) {
        const char *id = text_of(st, "id");
        const cJSON *bcs = cJSON_GetObjectItemCaseSensitive(st, "broadcasts"), *bc, *other;
        if (!in_list(follow, id)) continue;
        cJSON_ArrayForEach(bc, bcs) {
            const char *utc = text_of(bc, "utc");
            int m = minutes_of(utc), d, next_gap = MAX_BROADCAST_MIN;
            char key[40];
            if (m < 0) continue;
            snprintf(key, sizeof(key), "%s:%s", id, utc);
            if (in_list(skip, key)) continue;
            /* it lasts until the next thing the station sends, whatever it is */
            cJSON_ArrayForEach(other, bcs) {
                int o = minutes_of(text_of(other, "utc")), gap;
                if (o < 0 || other == bc) continue;
                gap = (o - m + 1440) % 1440;
                if (gap > 0 && gap < next_gap) next_gap = gap;
            }
            if (next_gap < MIN_BROADCAST_MIN) next_gap = MIN_BROADCAST_MIN;
            for (d = -1; d <= 1; d++) {
                time_t start = day + (time_t) d * 86400 + (time_t) m * 60, end = start + next_gap * 60;
                const cJSON *v, *freqs, *fr;
                if (end <= now) continue;
                if (found && start >= best_start) continue;
                found = 1; best_start = start;
                memset(out, 0, sizeof(*out));
                snprintf(out->station, sizeof(out->station), "%s", id);
                snprintf(out->name, sizeof(out->name), "%s", text_of(st, "name"));
                snprintf(out->title, sizeof(out->title), "%s", text_of(bc, "title"));
                v = cJSON_GetObjectItemCaseSensitive(bc, "lpm"); out->lpm = cJSON_IsNumber(v) ? v->valueint : 120;
                v = cJSON_GetObjectItemCaseSensitive(bc, "ioc"); out->ioc = cJSON_IsNumber(v) ? v->valueint : 576;
                v = cJSON_GetObjectItemCaseSensitive(st, "deviation"); out->deviation = cJSON_IsNumber(v) ? v->valueint : 400;
                out->start = start; out->end = end;
                freqs = cJSON_GetObjectItemCaseSensitive(st, "freqs");
                cJSON_ArrayForEach(fr, freqs) {
                    const cJSON *k = cJSON_GetObjectItemCaseSensitive(fr, "khz");
                    int from = minutes_of(text_of(fr, "from")), to = minutes_of(text_of(fr, "to"));
                    if (!cJSON_IsNumber(k) || out->nfreqs >= SCHEDULE_MAX_FREQS) continue;
                    /* a frequency that is only on the air part of the day */
                    if (from >= 0 && to >= 0) {
                        int on = from <= to ? (m >= from && m < to) : (m >= from || m < to);
                        if (!on) continue;
                    }
                    out->freqs[out->nfreqs++] = k->valuedouble;
                }
                out->suggested_khz = out->nfreqs > 0 ? out->freqs[out->nfreqs / 2] : 0;
                out->why = 'm'; out->distance_km = -1; out->reliability = -1;
                {
                    const cJSON *la = cJSON_GetObjectItemCaseSensitive(st, "lat"), *lo = cJSON_GetObjectItemCaseSensitive(st, "lon");
                    if (have_pos && cJSON_IsNumber(la) && cJSON_IsNumber(lo) && out->nfreqs > 0) {
                        time_t t = start > now ? start : now;
                        out->suggested_khz = schedule_pick(out->freqs, out->nfreqs, pos_lat, pos_lon, la->valuedouble, lo->valuedouble, t, &out->why);
                        out->distance_km = schedule_distance_km(pos_lat, pos_lon, la->valuedouble, lo->valuedouble);
                    }
                    if (out->nfreqs > 0) {
                        double r = -1, k = from_table(id, out->freqs, out->nfreqs, start > now ? start : now, &r);
                        if (k > 0) { out->suggested_khz = k; out->why = 'v'; out->reliability = r; }
                    }
                }
            }
        }
    }
    return found && out->nfreqs > 0;
}
