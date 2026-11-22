/*
 * Wefax · sharing one receiver with Navtex (EA7JRS/Navtex)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * A receiver can only be used by one program at a time. When Wefax shares its receiver with
 * Navtex, it asks Navtex to switch that receiver off before a facsimile broadcast and on again
 * after it, through the settings of Navtex's web interface (rx1.enabled / rx2.enabled).
 */
#ifndef WEFAX_NAVTEX_SHARE_H
#define WEFAX_NAVTEX_SHARE_H

/* base: "http://127.0.0.1:8080". rx: 1 or 2. on: 1 gives the receiver back to Navtex, 0 takes it.
   Returns 0 when Navtex answered that it is done, -1 otherwise. */
int navtex_set_receiver(const char *base, int rx, int on);

/* Splits "http://host:port" into its parts; 0 when it is a valid address (used by the tests) */
int navtex_parse_base(const char *base, char *host, int host_size, char *port, int port_size);

#endif
