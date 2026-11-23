/*
 * Wefax · the database (see store.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "store.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>
#include <sqlite3.h>

static char data_dir[512] = DATA_DIR;

void store_set_dir(const char *dir) { snprintf(data_dir, sizeof(data_dir), "%s", dir); }
const char *store_dir(void) { return data_dir; }

static sqlite3 *open_db(void)
{
    char path[600];
    sqlite3 *db = NULL;
    snprintf(path, sizeof(path), "%s/Wefax.db", data_dir);
    if (sqlite3_open(path, &db) != SQLITE_OK) { if (db) sqlite3_close(db); return NULL; }
    sqlite3_busy_timeout(db, 5000);
    return db;
}

int store_init(void)
{
    char path[600];
    sqlite3 *db;
    int ok;
    mkdir(data_dir, 0775);
    snprintf(path, sizeof(path), "%s/charts", data_dir);
    mkdir(path, 0775);
    db = open_db();
    if (db == NULL) return -1;
    ok = sqlite3_exec(db,
        "create table if not exists config (tag text primary key, value text);"
        "create table if not exists status (tag text primary key, value text, at integer);"
        "create table if not exists charts (id integer primary key autoincrement, received integer, station text, title text,"
        " khz real, ioc integer, lpm integer, width integer, height integer, ended text, offset_hz real, clock_ppm real,"
        " phased integer, file text, keep integer default 0);"
        "insert or ignore into config (tag, value) values ('settings_version', '1');",
        NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
    sqlite3_close(db);
    return ok;
}

void store_get(const char *name, char *out, size_t out_size, const char *def)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    snprintf(out, out_size, "%s", def != NULL ? def : "");
    if (db == NULL) return;
    if (sqlite3_prepare_v2(db, "select value from config where tag = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0) != NULL)
            snprintf(out, out_size, "%s", (const char *) sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
}

static int all_of(const char *s, const char *set, size_t max)
{
    return strlen(s) <= max && strspn(s, set) == strlen(s);
}

static int is_number(const char *s, double lo, double hi)
{
    char *end;
    double v;
    if (s[0] == '\0' || !all_of(s, "0123456789.-", 12)) return 0;
    v = strtod(s, &end);
    return *end == '\0' && v >= lo && v <= hi;
}

#define LETTERS "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define DIGITS "0123456789"

/* The settings there are, and what each may hold. Anything else is refused: the web interface
   has no password, so it must not be a way to write whatever into the database. */
int store_setting_valid(const char *name, const char *value)
{
    if (strcmp(name, "rx.enabled") == 0 || strcmp(name, "rx.bias") == 0 || strcmp(name, "share.on") == 0 || strcmp(name, "rx.listen") == 0)
        return strcmp(value, "0") == 0 || strcmp(value, "1") == 0;
    if (strcmp(name, "rx.source") == 0) return strcmp(value, "sdrplay") == 0 || strcmp(value, "rtlsdr") == 0 || strcmp(value, "kiwi") == 0 || strcmp(value, "hackrf") == 0;
    /* a HackRF One: its two gains (it has no automatic one) and its RF amplifier */
    if (strcmp(name, "rx.hackrf_lna") == 0) return is_number(value, 0, 40) && strchr(value, '.') == NULL && value[0] != '-';
    if (strcmp(name, "rx.hackrf_vga") == 0) return is_number(value, 0, 62) && strchr(value, '.') == NULL && value[0] != '-';
    if (strcmp(name, "rx.hackrf_amp") == 0) return strcmp(value, "0") == 0 || strcmp(value, "1") == 0;
    /* a KiwiSDR on the network: its name or address, port and password (no spaces: it goes in a command as it is) */
    if (strcmp(name, "rx.kiwi_host") == 0) return all_of(value, LETTERS DIGITS "abcdefghijklmnopqrstuvwxyz.-:", 80);
    if (strcmp(name, "rx.kiwi_port") == 0) return is_number(value, 1, 65535) && strchr(value, '.') == NULL && value[0] != '-';
    if (strcmp(name, "rx.kiwi_pass") == 0) return all_of(value, LETTERS DIGITS "abcdefghijklmnopqrstuvwxyz._-+!@*", 40);
    if (strcmp(name, "rx.port") == 0) return strlen(value) == 1 && strchr("ABC", value[0]) != NULL;
    if (strcmp(name, "rx.serial") == 0) return all_of(value, LETTERS DIGITS "abcdefghijklmnopqrstuvwxyz_-", 32);
    if (strcmp(name, "rx.direct") == 0) return strlen(value) == 1 && strchr("a012", value[0]) != NULL;
    if (strcmp(name, "rx.upconv") == 0) return is_number(value, 0, 500000000.0) && strchr(value, '.') == NULL && value[0] != '-';
    if (strcmp(name, "rx.gain") == 0) return strcmp(value, "auto") == 0 || is_number(value, 0, 60);
    if (strcmp(name, "rx.ppm") == 0) return is_number(value, -200, 200) && strchr(value, '.') == NULL;
    /* a fixed frequency instead of the schedule, in kHz; 0 = follow the schedule */
    if (strcmp(name, "rx.fixed_khz") == 0) return is_number(value, 0, 30000);
    if (strcmp(name, "rx.fixed_lpm") == 0) return strcmp(value, "60") == 0 || strcmp(value, "90") == 0 || strcmp(value, "120") == 0 || strcmp(value, "240") == 0;
    /* stations followed: ids from the schedule, separated by commas */
    if (strcmp(name, "follow") == 0) return all_of(value, LETTERS DIGITS ",_-", 400);
    /* broadcasts not wanted: STATION:HHMM, separated by commas */
    if (strcmp(name, "skip") == 0) return all_of(value, LETTERS DIGITS ",:_-", 4000);
    /* VOACAP predictions: smoothed sunspot number of the month (empty: not known, no predictions),
       man-made noise at 3 MHz in -dBW and signal to noise ratio needed in dB-Hz */
    if (strcmp(name, "prop.ssn") == 0) return value[0] == '\0' || (is_number(value, 0, 300) && value[0] != '-');
    /* the one tools/actualizar-manchas.sh reads from NOAA every day, used while prop.ssn is empty, and its date */
    if (strcmp(name, "prop.ssn_auto") == 0) return value[0] == '\0' || (is_number(value, 0, 300) && value[0] != '-');
    if (strcmp(name, "prop.ssn_date") == 0) return all_of(value, DIGITS "-", 10);
    if (strcmp(name, "prop.noise") == 0) return is_number(value, 110, 170) && strchr(value, '.') == NULL && value[0] != '-';
    if (strcmp(name, "prop.snr") == 0) return is_number(value, 10, 80) && strchr(value, '.') == NULL && value[0] != '-';
    /* space weather now, sent by S.A.T.-APRS+: NOAA scales and the unix time they were read, "R1S0G2T1795370000" */
    if (strcmp(name, "space.wx") == 0)
        return value[0] == '\0' || (strlen(value) >= 16 && strlen(value) <= 18 && value[0] == 'R' && strchr("012345", value[1]) && value[2] == 'S' && strchr("012345", value[3])
            && value[4] == 'G' && strchr("012345", value[5]) && value[6] == 'T' && all_of(value + 7, DIGITS, 11));
    /* where the receiver is: "lat,lon" in decimal degrees, or empty */
    if (strcmp(name, "position") == 0) {
        double la, lo; char extra;
        if (value[0] == '\0') return 1;
        return all_of(value, DIGITS ".,-", 24) && sscanf(value, "%lf,%lf%c", &la, &lo, &extra) == 2 && la >= -90 && la <= 90 && lo >= -180 && lo <= 180;
    }
    /* whether the position is the one gpsd gives (gps) or the one written (manual) */
    if (strcmp(name, "position_source") == 0) return strcmp(value, "gps") == 0 || strcmp(value, "manual") == 0;
    if (strcmp(name, "retention_days") == 0) return is_number(value, 1, 3650) && strchr(value, '.') == NULL;
    /* sharing the receiver with Navtex: its address and which of its receivers is the shared one */
    if (strcmp(name, "share.url") == 0)
        return value[0] == '\0' || (strncmp(value, "http://", 7) == 0 && all_of(value + 7, LETTERS DIGITS "abcdefghijklmnopqrstuvwxyz.-:", 80) && strlen(value) > 8);
    if (strcmp(name, "share.rx") == 0) return strcmp(value, "1") == 0 || strcmp(value, "2") == 0;
    /* the frequency chosen for a station: freq.<ID> = kHz, or "auto" */
    if (strncmp(name, "freq.", 5) == 0 && all_of(name + 5, LETTERS DIGITS "_-", 16) && name[5] != '\0')
        return strcmp(value, "auto") == 0 || is_number(value, 100, 30000);
    return 0;
}

int store_set(const char *name, const char *value)
{
    sqlite3 *db;
    sqlite3_stmt *st;
    int ok = -1;
    if (!store_setting_valid(name, value)) return -1;
    db = open_db();
    if (db == NULL) return -1;
    if (sqlite3_prepare_v2(db, "insert into config (tag, value) values (?1, ?2) on conflict(tag) do update set value = ?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, value, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_DONE) ok = 0;
        sqlite3_finalize(st);
    }
    if (ok == 0) sqlite3_exec(db, "update config set value = cast(value as integer) + 1 where tag = 'settings_version'", NULL, NULL, NULL);
    sqlite3_close(db);
    return ok;
}

long store_settings_version(void)
{
    char v[24];
    store_get("settings_version", v, sizeof(v), "0");
    return atol(v);
}

void store_status(const char *name, const char *value)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    if (db == NULL) return;
    if (sqlite3_prepare_v2(db, "insert into status (tag, value, at) values (?1, ?2, strftime('%s','now')) on conflict(tag) do update set value = ?2, at = strftime('%s','now')", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, value, -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
}

void store_status_num(const char *name, double value)
{
    char b[40];
    snprintf(b, sizeof(b), "%.3f", value);
    store_status(name, b);
}

long store_add_chart(const struct chart_info *c)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    long id = -1;
    if (db == NULL) return -1;
    if (sqlite3_prepare_v2(db, "insert into charts (received, station, title, khz, ioc, lpm, width, height, ended, offset_hz, clock_ppm, phased, file)"
                               " values (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, (sqlite3_int64) c->received);
        sqlite3_bind_text(st, 2, c->station, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, c->title, -1, SQLITE_STATIC);
        sqlite3_bind_double(st, 4, c->khz);
        sqlite3_bind_int(st, 5, c->ioc);
        sqlite3_bind_int(st, 6, c->lpm);
        sqlite3_bind_int(st, 7, c->width);
        sqlite3_bind_int(st, 8, c->height);
        sqlite3_bind_text(st, 9, c->ended, -1, SQLITE_STATIC);
        sqlite3_bind_double(st, 10, c->offset_hz);
        sqlite3_bind_double(st, 11, c->clock_ppm);
        sqlite3_bind_int(st, 12, c->phased);
        sqlite3_bind_text(st, 13, c->file, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_DONE) id = (long) sqlite3_last_insert_rowid(db);
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return id;
}

static char *print_and_free(cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    return s;
}

char *store_charts_json(long since_id, int limit)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    cJSON *list = cJSON_CreateArray();
    if (db != NULL && sqlite3_prepare_v2(db, "select id, received, station, title, khz, ioc, lpm, width, height, ended, offset_hz, clock_ppm, phased, keep"
                                             " from charts where id > ?1 order by id desc limit ?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, since_id);
        sqlite3_bind_int(st, 2, limit > 0 && limit <= 1000 ? limit : 200);
        while (sqlite3_step(st) == SQLITE_ROW) {
            cJSON *c = cJSON_CreateObject();
            time_t t = (time_t) sqlite3_column_int64(st, 1);
            struct tm tm;
            char when[32];
            gmtime_r(&t, &tm);
            strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", &tm);
            cJSON_AddNumberToObject(c, "id", (double) sqlite3_column_int64(st, 0));
            cJSON_AddStringToObject(c, "received", when);
            cJSON_AddStringToObject(c, "station", (const char *) sqlite3_column_text(st, 2));
            cJSON_AddStringToObject(c, "title", (const char *) sqlite3_column_text(st, 3));
            cJSON_AddNumberToObject(c, "khz", sqlite3_column_double(st, 4));
            cJSON_AddNumberToObject(c, "ioc", sqlite3_column_int(st, 5));
            cJSON_AddNumberToObject(c, "lpm", sqlite3_column_int(st, 6));
            cJSON_AddNumberToObject(c, "width", sqlite3_column_int(st, 7));
            cJSON_AddNumberToObject(c, "height", sqlite3_column_int(st, 8));
            cJSON_AddStringToObject(c, "ended", (const char *) sqlite3_column_text(st, 9));
            cJSON_AddNumberToObject(c, "offset_hz", sqlite3_column_double(st, 10));
            cJSON_AddNumberToObject(c, "clock_ppm", sqlite3_column_double(st, 11));
            cJSON_AddBoolToObject(c, "phased", sqlite3_column_int(st, 12) != 0);
            cJSON_AddBoolToObject(c, "symmetric", sqlite3_column_int(st, 12) == 2);
            cJSON_AddBoolToObject(c, "keep", sqlite3_column_int(st, 13) != 0);
            cJSON_AddItemToArray(list, c);
        }
        sqlite3_finalize(st);
    }
    if (db != NULL) sqlite3_close(db);
    return print_and_free(list);
}

char *store_status_json(void)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    cJSON *o = cJSON_CreateObject();
    if (db != NULL && sqlite3_prepare_v2(db, "select tag, value, at from status", -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "value", (const char *) sqlite3_column_text(st, 1));
            cJSON_AddNumberToObject(e, "at", (double) sqlite3_column_int64(st, 2));
            cJSON_AddItemToObject(o, (const char *) sqlite3_column_text(st, 0), e);
        }
        sqlite3_finalize(st);
    }
    if (db != NULL) sqlite3_close(db);
    return print_and_free(o);
}

char *store_settings_json(void)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    cJSON *o = cJSON_CreateObject();
    if (db != NULL && sqlite3_prepare_v2(db, "select tag, value from config", -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW)
            cJSON_AddStringToObject(o, (const char *) sqlite3_column_text(st, 0), (const char *) sqlite3_column_text(st, 1));
        sqlite3_finalize(st);
    }
    if (db != NULL) sqlite3_close(db);
    return print_and_free(o);
}

int store_keep_chart(long id, int keep)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    int r = -1;
    if (db == NULL) return -1;
    if (sqlite3_prepare_v2(db, "update charts set keep = ?2 where id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, id);
        sqlite3_bind_int(st, 2, keep ? 1 : 0);
        if (sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) == 1) r = 0;
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return r;
}

int store_chart_file(long id, char *out, size_t out_size)
{
    sqlite3 *db = open_db();
    sqlite3_stmt *st;
    int r = -1;
    if (db == NULL) return -1;
    if (sqlite3_prepare_v2(db, "select file from charts where id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0) != NULL) { snprintf(out, out_size, "%s", (const char *) sqlite3_column_text(st, 0)); r = 0; }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return r;
}

int store_purge(void)
{
    char days[16];
    sqlite3 *db;
    sqlite3_stmt *st;
    int n = 0;
    store_get("retention_days", days, sizeof(days), "30");
    db = open_db();
    if (db == NULL) return 0;
    if (sqlite3_prepare_v2(db, "select id, file from charts where keep = 0 and received < strftime('%s','now') - ?1 * 86400", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, atoi(days) > 0 ? atoi(days) : 30);
        while (sqlite3_step(st) == SQLITE_ROW) {
            char path[1200], sql[80];
            const char *file = (const char *) sqlite3_column_text(st, 1);
            /* only files of ours: a name without ".." under the charts directory */
            if (file != NULL && strstr(file, "..") == NULL && file[0] != '/') {
                snprintf(path, sizeof(path), "%s/charts/%s", data_dir, file);
                unlink(path);
            }
            snprintf(sql, sizeof(sql), "delete from charts where id = %lld", (long long) sqlite3_column_int64(st, 0));
            sqlite3_exec(db, sql, NULL, NULL, NULL);
            n++;
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return n;
}
