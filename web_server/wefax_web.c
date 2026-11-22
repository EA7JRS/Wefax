/*
 * Wefax · web server: the web interface, the charts and a small JSON API
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 *   wefaxwebserver [port] [listen address]      (8081, all addresses)
 *
 *   GET  /api/status                 what the receiver is doing
 *   GET  /api/charts?since=&limit=   the charts received, newest first
 *   GET  /api/about                  the version of the program
 *   GET  /api/gps                    the position gpsd gives (WEFAX_GPSD=host:port; empty, none)
 *   GET  /api/next                   the broadcast in progress or the next one
 *   GET  /api/settings               the settings
 *   POST /api/settings/<name>/<value>      changes one (header X-Wefax needed)
 *   POST /api/charts/keep/<id>/<0|1>       tags a chart to be kept (header X-Wefax needed)
 *   GET  /chart/<id>.png             a chart
 *   GET  /live.png                   the chart being received
 *   anything else: a file of the interface (WEB_ROOT)
 *
 * There is no password: it is meant for a local network that is trusted. Changes need POST and
 * a header a page of another site cannot send; files cannot be asked outside their folders.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <microhttpd.h>
#include <cjson/cJSON.h>
#include "../receiver/store.h"
#include "../receiver/schedule.h"
#include "gps.h"

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "?"
#endif
#ifndef WEB_ROOT
#define WEB_ROOT "/opt/Wefax/build"
#endif
#define MAX_URL_LEN 400
#define JSON "application/json"
#define ERR_400 "{\"error\":\"bad request\"}"
#define ERR_403 "{\"error\":\"changes need POST and the X-Wefax header\"}"
#define ERR_404 "{\"error\":\"not found\"}"
#define ERR_500 "{\"error\":\"database not available\"}"

static enum MHD_Result send_text(struct MHD_Connection *c, unsigned int status, const char *body, const char *type)
{
    struct MHD_Response *r = MHD_create_response_from_buffer(strlen(body), (void *) body, MHD_RESPMEM_MUST_COPY);
    enum MHD_Result ret;
    if (r == NULL) return MHD_NO;
    MHD_add_response_header(r, "Content-Type", type);
    MHD_add_response_header(r, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header(r, "Cache-Control", "no-store");
    ret = MHD_queue_response(c, status, r);
    MHD_destroy_response(r);
    return ret;
}

static enum MHD_Result send_json_free(struct MHD_Connection *c, char *json)
{
    enum MHD_Result ret;
    if (json == NULL) return send_text(c, MHD_HTTP_INTERNAL_SERVER_ERROR, ERR_500, JSON);
    ret = send_text(c, MHD_HTTP_OK, json, JSON);
    free(json);
    return ret;
}

static ssize_t file_reader(void *cls, uint64_t pos, char *buf, size_t max)
{
    FILE *f = cls;
    if (fseek(f, (long) pos, SEEK_SET) != 0) return MHD_CONTENT_READER_END_WITH_ERROR;
    {
        size_t n = fread(buf, 1, max, f);
        return n > 0 ? (ssize_t) n : MHD_CONTENT_READER_END_OF_STREAM;
    }
}
static void file_close(void *cls) { fclose((FILE *) cls); }

static const char *type_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html; charset=UTF-8";
    if (strcmp(dot, ".js") == 0) return "text/javascript; charset=UTF-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=UTF-8";
    if (strcmp(dot, ".json") == 0) return "application/json";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcmp(dot, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

/* Only plain names and folders: no "..", no "//", nothing starting with a dot or a slash */
static int safe_path(const char *p)
{
    size_t i;
    if (p[0] == '\0' || p[0] == '/' || p[0] == '.') return 0;
    for (i = 0; p[i] != '\0'; i++) {
        unsigned char ch = (unsigned char) p[i];
        if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/')) return 0;
        if (ch == '.' && p[i + 1] == '.') return 0;
        if (ch == '/' && (p[i + 1] == '/' || p[i + 1] == '.' || p[i + 1] == '\0')) return 0;
    }
    return 1;
}

static enum MHD_Result send_file(struct MHD_Connection *c, const char *path, int fresh)
{
    struct stat st;
    FILE *f = NULL;
    struct MHD_Response *r;
    enum MHD_Result ret;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) f = fopen(path, "rb");
    if (f == NULL) return send_text(c, MHD_HTTP_NOT_FOUND, ERR_404, JSON);
    r = MHD_create_response_from_callback((uint64_t) st.st_size, 32 * 1024, &file_reader, f, &file_close);
    if (r == NULL) { fclose(f); return MHD_NO; }
    MHD_add_response_header(r, "Content-Type", type_of(path));
    MHD_add_response_header(r, "X-Content-Type-Options", "nosniff");
    /* the interface only uses its own files: nothing received by radio can load or run anything */
    MHD_add_response_header(r, "Content-Security-Policy", "default-src 'self'; img-src 'self' data:; frame-ancestors 'none'");
    MHD_add_response_header(r, "Cache-Control", fresh ? "no-store" : "no-cache");
    ret = MHD_queue_response(c, MHD_HTTP_OK, r);
    MHD_destroy_response(r);
    return ret;
}

/* Digits only, up to max; -1 otherwise */
static long number_of(const char *s, const char **end, long max)
{
    long v = 0;
    if (!isdigit((unsigned char) *s)) return -1;
    while (isdigit((unsigned char) *s)) { v = v * 10 + (*s - '0'); if (v > max) return -1; s++; }
    if (end != NULL) *end = s;
    return v;
}

static void iso_utc(time_t t, char *out, size_t n)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static char *next_json(void)
{
    char follow[512], skip[4096], t[32];
    struct broadcast b;
    cJSON *o = cJSON_CreateObject();
    char *s;
    int i;
    if (o == NULL) return NULL;
    store_get("follow", follow, sizeof(follow), "");
    store_get("skip", skip, sizeof(skip), "");
    if (schedule_next(time(NULL), follow, skip, &b)) {
        cJSON *f = cJSON_AddArrayToObject(o, "freqs");
        cJSON_AddStringToObject(o, "station", b.station);
        cJSON_AddStringToObject(o, "name", b.name);
        cJSON_AddStringToObject(o, "title", b.title);
        iso_utc(b.start, t, sizeof(t)); cJSON_AddStringToObject(o, "start", t);
        iso_utc(b.end, t, sizeof(t)); cJSON_AddStringToObject(o, "end", t);
        cJSON_AddNumberToObject(o, "lpm", b.lpm);
        cJSON_AddNumberToObject(o, "ioc", b.ioc);
        cJSON_AddNumberToObject(o, "suggested_khz", b.suggested_khz);
        for (i = 0; i < b.nfreqs; i++) cJSON_AddItemToArray(f, cJSON_CreateNumber(b.freqs[i]));
    }
    s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

static enum MHD_Result handler(void *cls, struct MHD_Connection *c, const char *url, const char *method, const char *version,
                               const char *upload, size_t *upload_size, void **ptr)
{
    static int seen;
    int post = strcmp(method, MHD_HTTP_METHOD_POST) == 0;
    int own = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "X-Wefax") != NULL;
    char path[600];
    (void) cls; (void) version; (void) upload;

    if (strcmp(method, MHD_HTTP_METHOD_GET) != 0 && !post) return MHD_NO;
    if (*ptr == NULL) { *ptr = &seen; return MHD_YES; }       /* first call: only the headers */
    if (*upload_size != 0) { *upload_size = 0; return MHD_YES; }   /* nothing is read from a body */
    if (strlen(url) > MAX_URL_LEN) return send_text(c, MHD_HTTP_BAD_REQUEST, ERR_400, JSON);

    if (strcmp(url, "/api/status") == 0) return send_json_free(c, store_status_json());
    if (strcmp(url, "/api/about") == 0) return send_text(c, MHD_HTTP_OK, "{\"version\":\"" PACKAGE_VERSION "\"}", JSON);
    if (strcmp(url, "/api/gps") == 0) return send_json_free(c, gps_json());
    if (strcmp(url, "/api/next") == 0) return send_json_free(c, next_json());
    if (strcmp(url, "/api/charts") == 0) {
        const char *since = MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "since");
        const char *limit = MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "limit");
        const char *end = "";
        long s = 0, l = 200;
        if (since != NULL && ((s = number_of(since, &end, 2000000000L)) < 0 || *end != '\0')) return send_text(c, MHD_HTTP_BAD_REQUEST, ERR_400, JSON);
        if (limit != NULL && ((l = number_of(limit, &end, 2000)) < 1 || *end != '\0')) return send_text(c, MHD_HTTP_BAD_REQUEST, ERR_400, JSON);
        return send_json_free(c, store_charts_json(s, (int) l));
    }
    if (strcmp(url, "/api/settings") == 0) return send_json_free(c, store_settings_json());
    if (strncmp(url, "/api/settings/", 14) == 0) {
        const char *name = url + 14, *slash = strchr(name, '/');
        char key[40];
        if (!post || !own) return send_text(c, MHD_HTTP_FORBIDDEN, ERR_403, JSON);
        if (slash == NULL || slash == name || (size_t) (slash - name) >= sizeof(key)) return send_text(c, MHD_HTTP_BAD_REQUEST, ERR_400, JSON);
        memcpy(key, name, (size_t) (slash - name)); key[slash - name] = '\0';
        /* a single "-" stands for an empty value (an address cannot end in a slash everywhere) */
        if (store_set(key, strcmp(slash + 1, "-") == 0 ? "" : slash + 1) != 0) return send_text(c, MHD_HTTP_BAD_REQUEST, ERR_400, JSON);
        return send_text(c, MHD_HTTP_OK, "{}", JSON);
    }
    if (strncmp(url, "/api/charts/keep/", 17) == 0) {
        const char *end = "";
        long id = number_of(url + 17, &end, 2000000000L);
        if (!post || !own) return send_text(c, MHD_HTTP_FORBIDDEN, ERR_403, JSON);
        if (id < 0 || end[0] != '/' || (end[1] != '0' && end[1] != '1') || end[2] != '\0') return send_text(c, MHD_HTTP_BAD_REQUEST, ERR_400, JSON);
        if (store_keep_chart(id, end[1] == '1') != 0) return send_text(c, MHD_HTTP_NOT_FOUND, ERR_404, JSON);
        return send_text(c, MHD_HTTP_OK, "{}", JSON);
    }
    if (strncmp(url, "/api/", 5) == 0) return send_text(c, MHD_HTTP_NOT_FOUND, ERR_404, JSON);
    if (post) return send_text(c, MHD_HTTP_FORBIDDEN, ERR_403, JSON);

    if (strncmp(url, "/chart/", 7) == 0) {
        const char *end = "";
        char file[300];
        long id = number_of(url + 7, &end, 2000000000L);
        if (id < 0 || strcmp(end, ".png") != 0) return send_text(c, MHD_HTTP_BAD_REQUEST, ERR_400, JSON);
        if (store_chart_file(id, file, sizeof(file)) != 0 || !safe_path(file)) return send_text(c, MHD_HTTP_NOT_FOUND, ERR_404, JSON);
        snprintf(path, sizeof(path), "%s/charts/%s", store_dir(), file);
        return send_file(c, path, 0);
    }
    if (strcmp(url, "/live.png") == 0) {
        snprintf(path, sizeof(path), "%s/charts/live.png", store_dir());
        return send_file(c, path, 1);
    }
    /* anything else is a file of the interface; the program runs inside WEB_ROOT */
    {
        const char *file = strcmp(url, "/") == 0 ? "index.html" : url + 1;
        if (!safe_path(file)) return send_text(c, MHD_HTTP_NOT_FOUND, ERR_404, JSON);
        return send_file(c, file, 0);
    }
}

int main(int argc, char *const *argv)
{
    struct MHD_Daemon *d;
    struct sockaddr_in addr;
    const char *address = argc == 3 ? argv[2] : NULL, *root = getenv("WEFAX_WEB_ROOT"), *dir = getenv("WEFAX_DATA_DIR");
    int port = 8081;
    unsigned int flags = MHD_USE_THREAD_PER_CONNECTION;
#ifdef MHD_USE_INTERNAL_POLLING_THREAD
    flags |= MHD_USE_INTERNAL_POLLING_THREAD;
#endif
    if (argc > 3 || (argc >= 2 && argv[1][0] == '-')) {
        fprintf(stderr, "Usage: %s [port] [listen address]\n  port            TCP port (8081)\n"
                        "  listen address  IPv4 address to listen on (all), e.g. 127.0.0.1\n"
                        "Environment: WEFAX_DATA_DIR (%s), WEFAX_WEB_ROOT (%s), WEFAX_GPSD (127.0.0.1:2947; empty: no GPS)\n", argv[0], DATA_DIR, WEB_ROOT);
        return 1;
    }
    if (argc >= 2 && ((port = atoi(argv[1])) < 1 || port > 65535)) { fprintf(stderr, "Error: invalid port '%s'\n", argv[1]); return 1; }
    if (root == NULL || root[0] == '\0') root = WEB_ROOT;
    if (dir != NULL && dir[0] != '\0') store_set_dir(dir);
    if (store_init() != 0) { fprintf(stderr, "Error: the database in %s cannot be used\n", store_dir()); return 1; }
    if (chdir(root) != 0) { perror("the folder of the interface"); return 1; }
    if (schedule_load("estaciones.json") < 0) printf("no schedule: %s/estaciones.json cannot be read\n", root);

    {
        const char *gpsd = getenv("WEFAX_GPSD");
        if (gpsd == NULL) gpsd = "127.0.0.1:2947";
        gps_start(gpsd);
        printf(gpsd[0] != '\0' ? "Position from gpsd at %s\n" : "Position from gpsd disabled%s\n", gpsd);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short) port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (address != NULL && inet_pton(AF_INET, address, &addr.sin_addr) != 1) { fprintf(stderr, "Error: invalid IPv4 listen address '%s'\n", address); return 1; }
    d = MHD_start_daemon(flags, (unsigned short) port, NULL, NULL, &handler, NULL,
                         MHD_OPTION_SOCK_ADDR, (struct sockaddr *) &addr, MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 120, MHD_OPTION_END);
    if (d == NULL) { fprintf(stderr, "Error: the web server could not start on port %d\n", port); return 1; }
    printf("Wefax web interface on %s port %d\n", address != NULL ? address : "all addresses", port);
    fflush(stdout);
    for (;;) sleep(60);
    MHD_stop_daemon(d);
    return 0;
}
