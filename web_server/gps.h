/*
 * Wefax web server: position from gpsd.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef WEFAX_GPS_H
#define WEFAX_GPS_H

/* Starts the background reader. address is "host:port" ("127.0.0.1:2947");
   NULL or "" disables it. */
void gps_start (const char *address);

/* What gpsd last said, as JSON (to be freed by the caller):
   {"enabled":true,"connected":true,"fix":3,"lat":..,"lon":..,"alt":..,"time":"..","age":2,"used":10,"visible":14,"hdop":0.9} */
char *gps_json (void);

#endif
