/*
 * Wefax · the database: settings, status and the list of charts received
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * One SQLite file (DATA_DIR/Wefax.db) shared by the receiver and the web interface. The charts
 * themselves are PNG files under DATA_DIR/charts.
 */
#ifndef WEFAX_STORE_H
#define WEFAX_STORE_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DATA_DIR
#define DATA_DIR "/opt/Wefax"
#endif

/* Another directory than the one compiled in (tests, -o). Call before anything else. */
void store_set_dir(const char *dir);
const char *store_dir(void);

/* Creates the database and its tables when they are missing. 0 when it can be used. */
int store_init(void);

/* Settings. get: the value, or def when it is not set. set: 0 when stored, -1 when the name or
   the value is not valid (see store_setting_valid). */
void store_get(const char *name, char *out, size_t out_size, const char *def);
int store_set(const char *name, const char *value);
int store_setting_valid(const char *name, const char *value);
/* A number that changes whenever a setting does */
long store_settings_version(void);

/* Status of the receiver, for the web interface: name = value, with the time it was said */
void store_status(const char *name, const char *value);
void store_status_num(const char *name, double value);

struct chart_info {
    time_t received;            /* when it began, UTC */
    const char *station;        /* id of the station in the schedule, "" when not known */
    const char *title;          /* what the schedule says the chart is, "" when not known */
    double khz;
    int ioc, lpm, width, height;
    const char *ended;          /* "stop", "long", "lost", "cut" */
    double offset_hz, clock_ppm;
    int phased;                 /* 1 when the start of the lines was found; 0 for a chart caught half way */
    const char *file;           /* path of the PNG under the charts directory */
};
/* Adds a chart; returns its id, or -1 */
long store_add_chart(const struct chart_info *c);

/* JSON, to be freed by the caller (NULL without memory):
   store_charts_json: [{"id":..,"received":"2026-11-22T04:30:00Z","station":"DDH",...}], newest first
   store_status_json: {"name":{"value":"..","at":unix time},...}
   store_settings_json: {"name":"value",...} */
char *store_charts_json(long since_id, int limit);
char *store_status_json(void);
char *store_settings_json(void);

/* Tags a chart to be kept (1) or not (0). 0 done, -1 no such chart. */
int store_keep_chart(long id, int keep);
/* The PNG of a chart, relative to the charts directory; 0 when found */
int store_chart_file(long id, char *out, size_t out_size);
/* Removes the charts older than the retention time (setting retention_days) that are not tagged
   to be kept, with their files. Returns how many. */
int store_purge(void);

#ifdef __cplusplus
}
#endif
#endif
