/*
 * Wefax web server: position from gpsd (the same reader as EA7JRS/Navtex).
 *
 * A background thread keeps one connection to gpsd, asks it to report in
 * JSON and remembers the last position (TPV) and satellite (SKY) reports.
 * The web interface reads them through /api/gps. Nothing is ever sent to the
 * GPS, and when gpsd is not there the thread just tries again later.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <cjson/cJSON.h>
#include "gps.h"

#define RETRY_SECONDS 10
#define LINE_MAX_LEN 16384

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char host[64] = "";
static char port[8] = "";
static int enabled = 0;
static int connected = 0;

static struct {
    int fix;                 /* 0 unknown, 1 none, 2 2D, 3 3D */
    int has_position;
    double lat, lon;
    int has_alt;
    double alt;
    char time[40];
    time_t received;         /* when the last TPV arrived */
    int has_sats;
    int used, visible;
    int has_hdop;
    double hdop;
} last;

static double number_of (const cJSON *o, const char *name, int *present)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive (o, name);
    if (cJSON_IsNumber (v) && isfinite (v->valuedouble)) { *present = 1; return v->valuedouble; }
    *present = 0;
    return 0.0;
}

static void handle_line (const char *line)
{
    cJSON *j = cJSON_Parse (line);
    const cJSON *cls;
    int ok;

    if (j == NULL) return;
    cls = cJSON_GetObjectItemCaseSensitive (j, "class");
    if (cJSON_IsString (cls) && strcmp (cls->valuestring, "TPV") == 0)
    {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive (j, "time");
        int has_lat, has_lon, has_alt, has_mode;
        double mode = number_of (j, "mode", &has_mode);
        double lat = number_of (j, "lat", &has_lat);
        double lon = number_of (j, "lon", &has_lon);
        double alt = number_of (j, "altMSL", &has_alt);
        if (!has_alt) alt = number_of (j, "alt", &has_alt);

        pthread_mutex_lock (&lock);
        last.fix = has_mode ? (int) mode : 0;
        last.received = time (NULL);
        /* a position is only believed with a 2D or 3D fix */
        if (last.fix >= 2 && has_lat && has_lon && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0)
        {
            last.has_position = 1;
            last.lat = lat;
            last.lon = lon;
            last.has_alt = has_alt && last.fix >= 3;
            last.alt = alt;
        }
        else
            last.has_position = 0;
        last.time[0] = '\0';
        if (cJSON_IsString (t)) snprintf (last.time, sizeof (last.time), "%s", t->valuestring);
        pthread_mutex_unlock (&lock);
    }
    else if (cJSON_IsString (cls) && strcmp (cls->valuestring, "SKY") == 0)
    {
        const cJSON *sats = cJSON_GetObjectItemCaseSensitive (j, "satellites");
        int has_used, has_visible, has_hdop;
        double used = number_of (j, "uSat", &has_used);
        double visible = number_of (j, "nSat", &has_visible);
        double hdop = number_of (j, "hdop", &has_hdop);

        if (cJSON_IsArray (sats) && (!has_used || !has_visible))
        {
            const cJSON *s;
            int u = 0, n = 0;
            cJSON_ArrayForEach (s, sats)
            {
                n++;
                if (cJSON_IsTrue (cJSON_GetObjectItemCaseSensitive (s, "used"))) u++;
            }
            used = u; visible = n; has_used = has_visible = 1;
        }
        pthread_mutex_lock (&lock);
        /* gpsd also sends SKY reports with only the precision figures */
        if (has_used && has_visible) { last.has_sats = 1; last.used = (int) used; last.visible = (int) visible; }
        if (has_hdop) { last.has_hdop = 1; last.hdop = hdop; }
        pthread_mutex_unlock (&lock);
    }
    (void) ok;
    cJSON_Delete (j);
}

static int connect_gpsd (void)
{
    struct addrinfo hints, *res, *a;
    struct timeval tv = { 15, 0 };
    int fd = -1;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo (host, port, &hints, &res) != 0) return -1;
    for (a = res; a != NULL; a = a->ai_next)
    {
        fd = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        /* gpsd reports every second: 15 s of silence means it is gone */
        setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));
        setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv));
        if (connect (fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close (fd);
        fd = -1;
    }
    freeaddrinfo (res);
    return fd;
}

static void *reader (void *arg)
{
    static const char watch[] = "?WATCH={\"enable\":true,\"json\":true}\n";
    char *line = malloc (LINE_MAX_LEN);
    size_t used;

    (void) arg;
    if (line == NULL) return NULL;
    for (;;)
    {
        int fd = connect_gpsd ();
        if (fd >= 0 && send (fd, watch, sizeof (watch) - 1, MSG_NOSIGNAL) == (ssize_t) (sizeof (watch) - 1))
        {
            pthread_mutex_lock (&lock);
            connected = 1;
            pthread_mutex_unlock (&lock);
            used = 0;
            for (;;)
            {
                ssize_t n = recv (fd, line + used, LINE_MAX_LEN - 1 - used, 0);
                char *nl;
                if (n <= 0) break;
                used += (size_t) n;
                line[used] = '\0';
                while ((nl = memchr (line, '\n', used)) != NULL)
                {
                    *nl = '\0';
                    handle_line (line);
                    used -= (size_t) (nl - line) + 1;
                    memmove (line, nl + 1, used);
                    line[used] = '\0';
                }
                if (used >= LINE_MAX_LEN - 1) used = 0;   /* a line longer than we accept: drop it */
            }
        }
        if (fd >= 0) close (fd);
        pthread_mutex_lock (&lock);
        connected = 0;
        memset (&last, 0, sizeof (last));
        pthread_mutex_unlock (&lock);
        sleep (RETRY_SECONDS);
    }
    return NULL;
}

void gps_start (const char *address)
{
    pthread_t thread;
    const char *colon;
    size_t host_len;

    if (address == NULL || address[0] == '\0') return;
    colon = strrchr (address, ':');
    host_len = colon != NULL ? (size_t) (colon - address) : strlen (address);
    if (host_len == 0 || host_len >= sizeof (host)) return;
    memcpy (host, address, host_len);
    host[host_len] = '\0';
    snprintf (port, sizeof (port), "%s", (colon != NULL && colon[1] != '\0') ? colon + 1 : "2947");
    enabled = 1;
    if (pthread_create (&thread, NULL, reader, NULL) == 0)
        pthread_detach (thread);
    else
        enabled = 0;
}

char *gps_json (void)
{
    cJSON *j = cJSON_CreateObject ();
    char *text, *copy;

    pthread_mutex_lock (&lock);
    cJSON_AddBoolToObject (j, "enabled", enabled);
    cJSON_AddBoolToObject (j, "connected", connected);
    if (connected && last.received != 0)
    {
        cJSON_AddNumberToObject (j, "fix", last.fix);
        cJSON_AddNumberToObject (j, "age", (double) (time (NULL) - last.received));
        if (last.has_position)
        {
            cJSON_AddNumberToObject (j, "lat", last.lat);
            cJSON_AddNumberToObject (j, "lon", last.lon);
            if (last.has_alt) cJSON_AddNumberToObject (j, "alt", last.alt);
        }
        if (last.time[0] != '\0') cJSON_AddStringToObject (j, "time", last.time);
    }
    if (connected && last.has_sats)
    {
        cJSON_AddNumberToObject (j, "used", last.used);
        cJSON_AddNumberToObject (j, "visible", last.visible);
    }
    if (connected && last.has_hdop) cJSON_AddNumberToObject (j, "hdop", last.hdop);
    pthread_mutex_unlock (&lock);

    text = cJSON_PrintUnformatted (j);
    copy = text != NULL ? strdup (text) : NULL;
    cJSON_free (text);
    cJSON_Delete (j);
    return copy;
}
