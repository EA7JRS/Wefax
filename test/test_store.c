/*
 * Wefax · tests of the database, the schedule and the address of Navtex
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../receiver/navtex_share.h"
#include "../receiver/schedule.h"
#include "../receiver/store.h"

static int checks, failed;
#define CHECK(c, what) do { checks++; if (!(c)) { failed++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

/* A schedule written for the test: the stations and times are made up */
static const char *SCHEDULE =
"{\"source\":\"test\",\"stations\":["
"{\"id\":\"AAA\",\"name\":\"Test One\",\"country\":\"Nowhere\",\"deviation\":425,"
" \"freqs\":[{\"khz\":3855,\"call\":\"AAA1\"},{\"khz\":7880,\"call\":\"AAA2\"},{\"khz\":13882.5,\"call\":\"AAA3\"}],"
" \"broadcasts\":[{\"utc\":\"0430\",\"title\":\"Surface analysis\",\"lpm\":120,\"ioc\":576},{\"utc\":\"0512\",\"title\":\"Forecast\",\"lpm\":120,\"ioc\":576},"
"                {\"utc\":\"0525\",\"title\":\"Second analysis\"},{\"utc\":\"2350\",\"title\":\"Late chart\",\"lpm\":60,\"ioc\":288}]},"
"{\"id\":\"BBB\",\"name\":\"Test Two\",\"country\":\"Nowhere\","
" \"freqs\":[{\"khz\":4610,\"call\":\"BBB\",\"from\":\"1800\",\"to\":\"0600\"},{\"khz\":8040,\"call\":\"BBB\"},{\"khz\":11086.5,\"call\":\"BBB\",\"from\":\"0600\",\"to\":\"1800\"}],"
" \"broadcasts\":[{\"utc\":\"0500\",\"title\":\"Night chart\"},{\"utc\":\"1200\",\"title\":\"Day chart\"}]}]}";

int main(void)
{
    char v[200], host[64], port[8];
    struct broadcast b;
    struct chart_info c;
    char *j;
    long id;
    FILE *f;
    /* 2026-11-22 00:00:00 UTC */
    const time_t day = 1795305600;

    if (system("rm -rf test_data && mkdir test_data") != 0) return 1;
    store_set_dir("test_data");
    CHECK(store_init() == 0, "the database is created");
    CHECK(store_init() == 0, "and opening it again does no harm");

    /* settings: only the ones there are, with values that make sense */
    store_get("rx.source", v, sizeof(v), "none");
    CHECK(strcmp(v, "none") == 0, "a setting not set gives the default");
    CHECK(store_set("rx.source", "rtlsdr") == 0 && store_set("rx.source", "hackrf") != 0, "rx.source");
    store_get("rx.source", v, sizeof(v), "none");
    CHECK(strcmp(v, "rtlsdr") == 0, "a setting is read back");
    CHECK(store_set("rx.enabled", "1") == 0 && store_set("rx.enabled", "yes") != 0, "rx.enabled");
    CHECK(store_set("rx.fixed_khz", "7880") == 0 && store_set("rx.fixed_khz", "13882.5") == 0 && store_set("rx.fixed_khz", "99999") != 0 && store_set("rx.fixed_khz", "x") != 0, "rx.fixed_khz");
    CHECK(store_set("follow", "DDH,GYA") == 0 && store_set("follow", "DDH;drop table") != 0, "follow");
    CHECK(store_set("skip", "DDH:0430,GYA:1200") == 0 && store_set("skip", "a b") != 0, "skip");
    CHECK(store_set("freq.DDH", "7880") == 0 && store_set("freq.DDH", "auto") == 0 && store_set("freq.", "7880") != 0 && store_set("freq.DDH", "1") != 0, "freq.<station>");
    CHECK(store_set("share.url", "http://127.0.0.1:8080") == 0 && store_set("share.url", "") == 0 && store_set("share.url", "http://a/b?c") != 0 && store_set("share.url", "ftp://x") != 0, "share.url");
    CHECK(store_set("rx.serial", "00000001") == 0 && store_set("rx.serial", "a b") != 0, "rx.serial");
    CHECK(store_set("retention_days", "30") == 0 && store_set("retention_days", "0") != 0, "retention_days");
    CHECK(store_set("anything", "1") != 0 && store_set("settings_version", "9") != 0, "unknown settings are refused");
    {
        long before = store_settings_version();
        store_set("rx.ppm", "12");
        CHECK(store_settings_version() == before + 1, "the version moves with every change");
        store_set("rx.ppm", "1.5");
        CHECK(store_settings_version() == before + 1, "and not with a refused one");
    }

    /* status */
    store_status("state", "listening");
    store_status_num("level", 0.25);
    j = store_status_json();
    CHECK(j != NULL && strstr(j, "\"state\":{\"value\":\"listening\"") != NULL && strstr(j, "\"level\"") != NULL, "status as JSON");
    free(j);
    j = store_settings_json();
    CHECK(j != NULL && strstr(j, "\"rx.source\":\"rtlsdr\"") != NULL, "settings as JSON");
    free(j);

    /* charts */
    memset(&c, 0, sizeof(c));
    c.received = day + 4 * 3600 + 30 * 60; c.station = "AAA"; c.title = "Surface analysis"; c.khz = 7880; c.ioc = 576; c.lpm = 120;
    c.width = 1810; c.height = 1200; c.ended = "stop"; c.offset_hz = 35.5; c.clock_ppm = -2.0; c.phased = 1; c.file = "20261122/043000_AAA_78800.png";
    id = store_add_chart(&c);
    CHECK(id == 1, "first chart");
    c.received += 3600; c.title = ""; c.file = "20261122/053000_AAA_78800.png"; c.phased = 0; c.ended = "cut";
    CHECK(store_add_chart(&c) == 2, "second chart");
    j = store_charts_json(0, 10);
    CHECK(j != NULL && strstr(j, "\"received\":\"2026-11-22T04:30:00Z\"") != NULL && strstr(j, "\"title\":\"Surface analysis\"") != NULL, "charts as JSON");
    CHECK(j != NULL && strstr(j, "\"id\":2") < strstr(j, "\"id\":1"), "newest first");
    free(j);
    j = store_charts_json(1, 10);
    CHECK(j != NULL && strstr(j, "\"id\":2") != NULL && strstr(j, "\"id\":1,") == NULL, "since an id");
    free(j);
    CHECK(store_chart_file(1, v, sizeof(v)) == 0 && strcmp(v, "20261122/043000_AAA_78800.png") == 0 && store_chart_file(9, v, sizeof(v)) != 0, "the file of a chart");
    CHECK(store_keep_chart(1, 1) == 0 && store_keep_chart(9, 1) != 0, "tagging a chart to keep");
    /* both charts are old (their date is the test's): the purge takes the one not kept, with its file */
    mkdir("test_data/charts/20261122", 0775);
    f = fopen("test_data/charts/20261122/053000_AAA_78800.png", "wb"); if (f) { fputs("x", f); fclose(f); }
    store_set("retention_days", "1");
    if (time(NULL) > c.received + 2 * 86400) {
        CHECK(store_purge() == 1, "the purge removes the old chart that is not kept");
        f = fopen("test_data/charts/20261122/053000_AAA_78800.png", "rb");
        CHECK(f == NULL, "and its file");
        if (f) fclose(f);
        j = store_charts_json(0, 10);
        CHECK(j != NULL && strstr(j, "\"id\":1") != NULL && strstr(j, "\"id\":2") == NULL, "the kept one stays");
        free(j);
    }

    /* the schedule */
    f = fopen("test_data/estaciones.json", "wb"); fputs(SCHEDULE, f); fclose(f);
    CHECK(schedule_load("test_data/none.json") < 0, "no file, no schedule");
    CHECK(schedule_load("test_data/estaciones.json") == 2, "two stations loaded");
    CHECK(!schedule_next(day, "", "", &b) && !schedule_next(day, "ZZZ", "", &b), "nothing followed, nothing next");
    CHECK(schedule_next(day + 3 * 3600, "AAA", "", &b) && strcmp(b.title, "Surface analysis") == 0 && b.start == day + 4 * 3600 + 30 * 60, "the next broadcast of AAA at 03:00");
    CHECK(b.end == b.start + 30 * 60 && b.lpm == 120 && b.ioc == 576 && b.deviation == 425, "it lasts 30 minutes at most; 425 Hz station");
    CHECK(b.nfreqs == 3 && b.suggested_khz == 7880, "its three frequencies; the middle one when not chosen");
    CHECK(schedule_next(day + 4 * 3600 + 40 * 60, "AAA", "", &b) && strcmp(b.title, "Surface analysis") == 0, "at 04:40 it is still that one");
    CHECK(schedule_next(day + 5 * 3600 + 13 * 60, "AAA", "", &b) && strcmp(b.title, "Forecast") == 0 && b.end == day + 5 * 3600 + 25 * 60, "the 05:12 one ends when the 05:25 begins");
    CHECK(b.lpm == 120 && schedule_next(day + 5 * 3600 + 30 * 60, "AAA", "", &b) && b.lpm == 120 && b.ioc == 576, "120 / 576 when the schedule does not say");
    CHECK(schedule_next(day + 3 * 3600, "AAA", "AAA:0430", &b) && strcmp(b.title, "Forecast") == 0, "a broadcast not wanted is skipped");
    CHECK(schedule_next(day + 3 * 3600, "AAA,BBB", "AAA:0430", &b) && strcmp(b.station, "BBB") == 0 && strcmp(b.title, "Night chart") == 0, "two stations: the earliest wins");
    CHECK(b.nfreqs == 2 && b.freqs[0] == 4610 && b.freqs[1] == 8040 && b.deviation == 400, "at night only the frequencies on the air then");
    CHECK(schedule_next(day + 11 * 3600, "BBB", "", &b) && b.nfreqs == 2 && b.freqs[0] == 8040 && b.freqs[1] == 11086.5, "by day, the others");
    CHECK(schedule_next(day + 23 * 3600, "AAA", "", &b) && strcmp(b.title, "Late chart") == 0 && b.lpm == 60 && b.ioc == 288, "the last of the day");
    CHECK(schedule_next(day + 23 * 3600 + 3540 + 1800, "AAA", "", &b) && b.start == day + 86400 + 4 * 3600 + 30 * 60, "after it, tomorrow's first");
    CHECK(schedule_next(day + 3 * 3600, "AA", "", &b) == 0, "part of an id is not the id");
    schedule_free();

    /* the address of Navtex */
    CHECK(navtex_parse_base("http://127.0.0.1:8080", host, sizeof(host), port, sizeof(port)) == 0 && strcmp(host, "127.0.0.1") == 0 && strcmp(port, "8080") == 0, "address with port");
    CHECK(navtex_parse_base("http://sdr.local", host, sizeof(host), port, sizeof(port)) == 0 && strcmp(port, "80") == 0, "address without port");
    CHECK(navtex_parse_base("https://x", host, sizeof(host), port, sizeof(port)) != 0 && navtex_parse_base("http://x/y", host, sizeof(host), port, sizeof(port)) != 0 &&
          navtex_parse_base("http://x:abc", host, sizeof(host), port, sizeof(port)) != 0 && navtex_parse_base("http://", host, sizeof(host), port, sizeof(port)) != 0, "bad addresses");
    CHECK(navtex_set_receiver("http://127.0.0.1:1", 1, 0) != 0 && navtex_set_receiver("http://127.0.0.1:8080", 3, 0) != 0, "nobody there, or no such receiver: not done");

    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
