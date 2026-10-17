/*
 * fldigi-feldhell-core test helpers (header-only).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FELDHELL_TEST_UTIL_H
#define FELDHELL_TEST_UTIL_H

#include "fldigi_feldhell.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures __attribute__((unused));

#define CHECK(cond, ...)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            failures++;                                                                  \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                         \
            fprintf(stderr, __VA_ARGS__);                                                \
            fputc('\n', stderr);                                                         \
        }                                                                                \
    } while (0)

/* The platform the goldens were made on (fldigi 4.2.13 bench, macOS arm64, Apple clang -O2
 * with its default FP contraction): there the port must reproduce fldigi bit for bit. */
#if defined(__APPLE__) && defined(__aarch64__) && !defined(FELDHELL_NOT_REFERENCE)
#define REFERENCE_PLATFORM 1
#else
#define REFERENCE_PLATFORM 0
#endif

static inline void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = 0;
    *len = (size_t)n;
    return buf;
}

typedef struct {
    double *x;
    size_t n, cap;
} sbuf;

static inline void sbuf_add(sbuf *b, const double *x, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 1024;
        b->x = (double *)realloc(b->x, b->cap * sizeof(double));
    }
    memcpy(b->x + b->n, x, n * sizeof(double));
    b->n += n;
}

/* A whole over as fldigi's bench sends it: begin, the text, end. Returns a malloc'ed buffer
 * of *n samples (peak 1.0, before fldigi's TX level). */
static inline double *render_cfg(const ffeldhell_tx_config *c, const char *text, int idle,
                                 size_t *n)
{
    ffeldhell_tx *tx = ffeldhell_tx_create(c);
    sbuf b = {NULL, 0, 0};
    double *tmp;
    size_t bound, k, i;
    if (!tx) {
        *n = 0;
        return NULL;
    }
    bound = ffeldhell_tx_bound(tx);
    tmp = (double *)malloc(bound * sizeof(double));
    k = ffeldhell_tx_begin(tx, tmp, bound);
    sbuf_add(&b, tmp, k);
    for (i = 0; text[i]; i++) {
        k = ffeldhell_tx_put(tx, (unsigned char)text[i], tmp, bound);
        sbuf_add(&b, tmp, k);
    }
    for (i = 0; i < (size_t)idle; i++) {
        k = ffeldhell_tx_idle(tx, tmp, bound);
        sbuf_add(&b, tmp, k);
    }
    k = ffeldhell_tx_end(tx, tmp, bound);
    sbuf_add(&b, tmp, k);
    free(tmp);
    ffeldhell_tx_destroy(tx);
    *n = b.n;
    return b.x;
}

static inline double *render(const char *submode, const char *text, int font, size_t *n)
{
    ffeldhell_tx_config c;
    ffeldhell_tx_config_default(&c, submode);
    c.font = font;
    return render_cfg(&c, text, 0, n);
}

/* fldigi's sound path: modem::ModulateXmtr scales by 10^(txlevel/20), txlevel -12 dB, and
 * clips at +-0.95 (trx/modem.cxx:652-658); libsndfile writes PCM16 as lrint(x * 0x7FFF);
 * the bench reads it back as int16 / 32768. */
static inline void quantize(const double *x, size_t n, int16_t *pcm)
{
    double mult = pow(10, -12.0 / 20.0);
    size_t i;
    for (i = 0; i < n; i++) {
        double v = x[i] * mult;
        if (v < -0.95)
            v = -0.95;
        if (v > 0.95)
            v = 0.95;
        pcm[i] = (int16_t)lrint(v * 32767.0);
    }
}

/* lead seconds of silence + the over at fldigi's level + tail seconds, as the bench reads. */
static inline double *pad_level(const double *x, size_t n, double lead_s, double tail_s,
                                size_t *out_n)
{
    size_t lead = (size_t)(lead_s * FFELDHELL_RATE), tail = (size_t)(tail_s * FFELDHELL_RATE);
    size_t total = lead + n + tail, i;
    double *y = (double *)calloc(total, sizeof(double));
    int16_t *pcm = (int16_t *)malloc((n ? n : 1) * sizeof(int16_t));
    quantize(x, n, pcm);
    for (i = 0; i < n; i++)
        y[lead + i] = pcm[i] / 32768.0;
    free(pcm);
    *out_n = total;
    return y;
}

typedef struct {
    char text[8192];
    double conf[8192];
    size_t n;            /* characters in text (no-match blocks excluded) */
    size_t nomatch;
    unsigned char *raster;
    size_t columns, raster_cap;
} rx_result;

static inline void collect(ffeldhell_rx *rx, const ffeldhell_char *ch, size_t got,
                           rx_result *r)
{
    size_t k, m, h = (size_t)ffeldhell_rx_height(rx);
    unsigned char col[64 * 42];
    for (k = 0; k < got; k++) {
        if (!ch[k].c) {
            r->nomatch++;
        } else if (r->n + 1 < sizeof r->text) {
            r->conf[r->n] = ch[k].confidence;
            r->text[r->n++] = (char)ch[k].c;
        }
    }
    while ((m = ffeldhell_rx_take_columns(rx, col, 64)) > 0) {
        if ((r->columns + m) * h > r->raster_cap) {
            r->raster_cap = (r->columns + m) * h * 2 + 4096;
            r->raster = (unsigned char *)realloc(r->raster, r->raster_cap);
        }
        memcpy(r->raster + r->columns * h, col, m * h);
        r->columns += m;
    }
    r->text[r->n] = 0;
}

/* Receive x in chunks of `chunk` samples, flush; collect the text (no-match blocks
 * counted, not written) and every raster column. */
static inline void receive(ffeldhell_rx *rx, const double *x, size_t n, size_t chunk,
                           rx_result *r)
{
    ffeldhell_char ch[512];
    size_t off, len;
    memset(r, 0, sizeof *r);
    for (off = 0; off < n; off += len) {
        len = n - off < chunk ? n - off : chunk;
        collect(rx, ch, ffeldhell_rx_process(rx, x + off, len, ch, 512), r);
    }
    collect(rx, ch, ffeldhell_rx_flush(rx, ch, 512), r);
}

static inline ffeldhell_rx *make_rx(const char *submode)
{
    ffeldhell_rx_config c;
    ffeldhell_rx_config_default(&c, submode);
    return ffeldhell_rx_create(&c);
}

/* Minimum edit distance between want and any substring of got (case-insensitive: fldigi's
 * default font draws lower case as capitals). */
static inline size_t window_distance(const char *want, const char *got)
{
    size_t nw = strlen(want), ng = strlen(got);
    size_t *prev = (size_t *)malloc((nw + 1) * sizeof(size_t));
    size_t *cur = (size_t *)malloc((nw + 1) * sizeof(size_t));
    size_t i, j, best;
    for (i = 0; i <= nw; i++)
        prev[i] = i;
    best = prev[nw];
    for (j = 1; j <= ng; j++) {
        cur[0] = 0;
        for (i = 1; i <= nw; i++) {
            size_t a = prev[i - 1] + (toupper((unsigned char)want[i - 1]) !=
                                      toupper((unsigned char)got[j - 1]));
            size_t b = prev[i] + 1, c = cur[i - 1] + 1;
            cur[i] = a < b ? (a < c ? a : c) : (b < c ? b : c);
        }
        if (cur[nw] < best)
            best = cur[nw];
        memcpy(prev, cur, (nw + 1) * sizeof(size_t));
    }
    free(prev);
    free(cur);
    return best;
}

#endif
