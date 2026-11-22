/*
 * Wefax · the schedule (see schedule.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#define MAX_BROADCAST_MIN 30
#define MIN_BROADCAST_MIN 8

static cJSON *root;

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
            }
        }
    }
    return found && out->nfreqs > 0;
}
