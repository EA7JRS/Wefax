/*
 * Wefax · predictions of a short wave circuit with VOACAP (see voacap.h)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#define _DEFAULT_SOURCE
#include "voacap.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static char dir[512];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static const char *const PROGRAMS[] = { "/usr/local/bin/voacapl", "/usr/bin/voacapl", NULL };

void voacap_set_dir(const char *itshfbc) { snprintf(dir, sizeof(dir), "%s", itshfbc); }

static const char *program(void)
{
    int i;
    for (i = 0; PROGRAMS[i] != NULL; i++) if (access(PROGRAMS[i], X_OK) == 0) return PROGRAMS[i];
    return NULL;
}

int voacap_available(void)
{
    char p[600];
    struct stat st;
    snprintf(p, sizeof(p), "%s/run", dir);
    return dir[0] != '\0' && program() != NULL && stat(p, &st) == 0 && S_ISDIR(st.st_mode) && access(p, W_OK) == 0;
}

int voacap_input(const struct voacap_in *in, char *text, int size)
{
    char freqs[VOACAP_MAX_FREQS * 5 + 1];
    int i, n;
    if (in->nfreqs < 1 || in->nfreqs > VOACAP_MAX_FREQS || in->month < 1 || in->month > 12 || in->year < 1990 || in->year > 2100) return -1;
    if (!(fabs(in->tx_lat) <= 90 && fabs(in->rx_lat) <= 90 && fabs(in->tx_lon) <= 180 && fabs(in->rx_lon) <= 180)) return -1;
    if (!(in->kw >= 0.001 && in->kw <= 1000) || !(in->ssn >= 0 && in->ssn <= 300) || !(in->noise >= 100 && in->noise <= 180) || !(in->snr >= 0 && in->snr <= 99)) return -1;
    for (i = 0; i < VOACAP_MAX_FREQS; i++) {
        double mhz = i < in->nfreqs ? in->khz[i] / 1000.0 : 0.0;
        /* VOACAP works from 2 to 30 MHz */
        if (i < in->nfreqs && !(mhz >= 2.0 && mhz <= 30.0)) return -1;
        snprintf(freqs + i * 5, 6, "%5.2f", mhz);
    }
    n = snprintf(text, (size_t) size,
        "LINEMAX      55\n"
        "COEFFS    CCIR\n"
        "TIME          1   24    1    1\n"
        "MONTH     %5d%6.2f\n"
        "SUNSPOT   %4.0f.\n"
        "LABEL     TRANSMITTER         RECEIVER\n"
        "CIRCUIT   %5.2f%c%9.2f%c%9.2f%c%9.2f%c  S     0\n"
        "SYSTEM       1.%4.0f. 3.00  90.%5.1f 3.00 0.10\n"
        "FPROB      1.00 1.00 1.00 0.00\n"
        "ANTENNA       1    1    2   30     0.000[default/isotrope     ]  0.0%10.4f\n"
        "ANTENNA       2    2    2   30     0.000[default/swwhip.voa   ]  0.0    0.0000\n"
        "FREQUENCY %s\n"
        "METHOD       30    0\n"
        "EXECUTE\n"
        "QUIT\n",
        in->year, (double) in->month, in->ssn,
        fabs(in->tx_lat), in->tx_lat < 0 ? 'S' : 'N', fabs(in->tx_lon), in->tx_lon < 0 ? 'W' : 'E',
        fabs(in->rx_lat), in->rx_lat < 0 ? 'S' : 'N', fabs(in->rx_lon), in->rx_lon < 0 ? 'W' : 'E',
        in->noise, in->snr, in->kw, freqs);
    return n > 0 && n < size ? n : -1;
}

/* The value number k of a row (0 is the column of the MUF, 1 the first frequency); NAN when there is none */
static double field(const char *line, size_t len, int k)
{
    char cell[6];
    char *end;
    size_t from = 6 + 5 * (size_t) k;      /* the hour takes six columns, each value five */
    double v;
    if (len < from + 5) return NAN;
    memcpy(cell, line + from, 5); cell[5] = '\0';
    v = strtod(cell, &end);
    return end == cell ? NAN : v;
}

int voacap_parse(const char *text, int nfreqs, struct voacap_out *out)
{
    const char *p = text;
    int hour = -1, hours = 0, f;
    for (f = 0; f < VOACAP_MAX_FREQS; f++) { int h; for (h = 0; h < 24; h++) { out->rel[f][h] = NAN; out->snr[f][h] = NAN; } }
    for (f = 0; f < 24; f++) out->muf[f] = NAN;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t) (nl - p) : strlen(p);
        /* the name of the row comes after the thirteen columns */
        const char *name = len > 66 ? p + 66 : "";
        size_t nlen = len > 66 ? len - 66 : 0;
        while (nlen > 0 && name[0] == ' ') { name++; nlen--; }
        while (nlen > 0 && (name[nlen - 1] == ' ' || name[nlen - 1] == '\r')) nlen--;
        if (nlen == 4 && strncmp(name, "FREQ", 4) == 0) {
            char cell[7];
            memcpy(cell, p, 6); cell[6] = '\0';
            hour = (int) (strtod(cell, NULL) + 0.5);
            if (hour < 1 || hour > 24) hour = -1;
            else { hour %= 24; out->muf[hour] = field(p, len, 0); hours++; }
        } else if (hour >= 0 && nlen == 3 && (strncmp(name, "REL", 3) == 0 || strncmp(name, "SNR", 3) == 0)) {
            for (f = 0; f < nfreqs; f++) {
                double v = field(p, len, f + 1);
                if (name[0] == 'R') out->rel[f][hour] = v; else out->snr[f][hour] = v;
            }
        }
        if (nl == NULL) break;
        p = nl + 1;
    }
    return hours;
}

int voacap_run(const struct voacap_in *in, struct voacap_out *out)
{
    static unsigned serial;
    char text[2000], name_in[64], name_out[64], path[700], *result = NULL;
    const char *prog = program();
    FILE *f;
    pid_t pid;
    int status = 0, n, ok = -3;
    long size;

    if (!voacap_available() || prog == NULL) return -1;
    n = voacap_input(in, text, (int) sizeof(text));
    if (n < 0) return -2;

    pthread_mutex_lock(&lock);
    snprintf(name_in, sizeof(name_in), "wefax%u.dat", ++serial % 8);
    snprintf(name_out, sizeof(name_out), "wefax%u.out", serial % 8);
    snprintf(path, sizeof(path), "%s/run/%s", dir, name_in);
    f = fopen(path, "w");
    if (f == NULL) { pthread_mutex_unlock(&lock); return -3; }
    fwrite(text, 1, (size_t) n, f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/run/%s", dir, name_out);
    unlink(path);

    pid = fork();
    if (pid == 0) {
        /* no shell: the program, its data folder and the two file names */
        int null = open("/dev/null", 2);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
        alarm(20);
        execl(prog, "voacapl", "-s", dir, name_in, name_out, (char *) NULL);
        _exit(127);
    }
    if (pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) != 127) {
        f = fopen(path, "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
            if (size > 0 && size < 4 * 1024 * 1024 && (result = malloc((size_t) size + 1)) != NULL) {
                size = (long) fread(result, 1, (size_t) size, f);
                result[size] = '\0';
                if (voacap_parse(result, in->nfreqs, out) == 24) ok = 0;
                free(result);
            }
            fclose(f);
        }
    }
    pthread_mutex_unlock(&lock);
    return ok;
}
