/*
 * Wefax · the schedule: which station sends what, when and where
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * The stations come from a JSON file (estaciones.json), made from the "Worldwide Marine
 * Radiofacsimile Broadcast Schedules" of the U.S. National Weather Service:
 *   {"source":"...","dated":"...","stations":[{"id":"DDH","name":"Hamburg / Pinneberg","country":"Germany",
 *     "lat":53.66,"lon":9.8,"deviation":425,
 *     "freqs":[{"khz":3855,"call":"DDH3"},{"khz":7880,"call":"DDK3","from":"0000","to":"2400"}],
 *     "broadcasts":[{"utc":"0430","title":"Surface analysis","lpm":120,"ioc":576}]}]}
 * Times are UTC. A frequency is the assigned one: the centre of the channel (in upper sideband a
 * receiver is tuned 1.9 kHz below it).
 */
#ifndef WEFAX_SCHEDULE_H
#define WEFAX_SCHEDULE_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCHEDULE_MAX_FREQS 8

struct broadcast {
    char station[24];           /* id */
    char name[80];              /* name of the station */
    char title[240];            /* what is sent */
    int lpm, ioc;
    int deviation;              /* Hz from the centre to black or white: 400, or 425 at some stations */
    time_t start, end;          /* end: when the next thing of that station begins, 30 minutes at most */
    double freqs[SCHEDULE_MAX_FREQS];   /* kHz on the air at that time, in the order of the list */
    int nfreqs;
    double suggested_khz;       /* the one to use when the user has not chosen */
};

/* Loads the file. Returns the number of stations, or -1 (not found, not JSON, nothing usable). */
int schedule_load(const char *path);
void schedule_free(void);

/* The broadcast in progress or, if none, the next one, among the stations in `follow` (ids
   separated by commas) and not in `skip` ("ID:HHMM,..."). Returns 1 when there is one. */
int schedule_next(time_t now, const char *follow, const char *skip, struct broadcast *out);

#ifdef __cplusplus
}
#endif
#endif
