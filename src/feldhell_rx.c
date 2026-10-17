/*
 * fldigi-feldhell-core receiver: feld::rx_init, mixer, FSKH_rx, rx and rx_process (fldigi
 * 4.2.13 src/feld/feld.cxx:69-118, 146-259, 268-292, 296-510) on fldigi-dsp-core's Hilbert
 * FIR, fftfilt and moving averages, plus the raster ring and the recogniser hand-off.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain; ported from fldigi 4.2.13 src/feld/feld.cxx
 * (c) 2006-2014 Dave Freese W1HKJ, with much owed to Joe Veldhuis N8FQ, adapted from gmfsk
 * (c) 2001-2004 Tomi Manninen OH2BNS and Lawrence Glaister VE7IT.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "feldhell_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RASTER_COLUMNS 4096
#define FFT_LEN 1024                    /* fftfilt(0, 0.5, 1024), feld.cxx:275 */

struct ffeldhell_rx {
    feld_params p;
    fdsp_fir hilbert;                   /* init_hilbert(37, 1), feld.cxx:278-279 */
    fdsp_fftfilt lpfilt;
    fdsp_movavgh average;               /* Cmovavg(200) + setLength, feld.cxx:236, 276 */
    fdsp_movavg bbfilt;                 /* Cmovavg(8), feld.cxx:277 */
    feld_recogniser *rec;
    double rxphacc;
    double frequency;
    double rxpixrate;
    double downsampleinc;
    double rxcounter;
    double agc;
    double peakval;
    double peakhold;
    double metric;
    double rx_bw;                       /* progdefaults.HELL_BW */
    fdsp_cmpx prev;
    int RxColumnLen;
    int col_pointer;
    int col_data[2 * FELD_MAX_RX_COLUMN_LEN];
    int hellagc, blackboard, reverse, squelch_on;
    double squelch;
    int average_len;
    /* raster ring of displayed columns */
    unsigned char *ring;
    size_t ring_head, ring_count, ring_dropped;
    /* output of the current call */
    ffeldhell_char *out;
    size_t max_out, n_out, dropped;
    size_t footprint;
};

void ffeldhell_rx_config_default(ffeldhell_rx_config *cfg, const char *submode)
{
    /* fldigi's defaults: configuration.h:735-770 (HellRcvHeight 20, hellagc 2,
     * HellBlackboard off), status.cxx:150-155 (squelch 5.0) and the bench (squelch off) */
    if (!cfg)
        return;
    memset(cfg, 0, sizeof *cfg);
    cfg->submode = submode;
    cfg->carrier_hz = 1500.0;
    cfg->rx_bw_hz = 0.0;
    cfg->height = 20;
    cfg->agc = 2;
    cfg->squelch = 5.0;
    cfg->font_mask = FELD_DEFAULT_FONT_MASK;
    cfg->floor = FELD_DEFAULT_FLOOR;
}

static void rx_init(ffeldhell_rx *rx)
{
    /* rx_init, feld.cxx:69-118: counters, peak hold, AGC and the column buffer;
     * rxpixrate = RxColumnLen * feldcolumnrate for every mode */
    int i;
    rx->rxcounter = 0.0;
    rx->peakhold = 0.0;
    for (i = 0; i < 2 * FELD_MAX_RX_COLUMN_LEN; i++)
        rx->col_data[i] = 0;
    rx->col_pointer = 0;
    rx->agc = 0.0;
    rx->rxpixrate = (rx->RxColumnLen * rx->p.feldcolumnrate);
    rx->downsampleinc = (double)(rx->rxpixrate / FELD_SAMPLE_RATE);
}

static void clear_state(ffeldhell_rx *rx)
{
    /* constructor state (feld.cxx:268-292: rxphacc 0, fresh filters, prev 0, peakval 0),
     * restart's filters (feld.cxx:235-236) and the rx_process bandwidth check
     * (feld.cxx:453-480, quirk Q10): fldigi starts the low pass at 1.0 x filter_bandwidth
     * and replaces it on the first block by 0.5 x HELL_BW whenever the two differ */
    memset(rx->hilbert.ibuffer, 0, sizeof rx->hilbert.ibuffer);
    memset(rx->hilbert.qbuffer, 0, sizeof rx->hilbert.qbuffer);
    rx->hilbert.pointer = rx->hilbert.length;
    rx->hilbert.counter = 0;
    if (rx->rx_bw != rx->p.filter_bandwidth)
        fdsp_fftfilt_create(&rx->lpfilt, 0, 0.5 * rx->rx_bw / FELD_SAMPLE_RATE);
    else
        fdsp_fftfilt_create(&rx->lpfilt, 0, 1.0 * rx->p.filter_bandwidth / FELD_SAMPLE_RATE);
    fdsp_movavgh_reset(&rx->average);
    fdsp_movavg_reset(&rx->bbfilt);
    rx->prev.re = rx->prev.im = 0.0;
    rx->rxphacc = 0.0;
    rx->peakval = 0.0;
    rx->metric = 0.0;
    rx_init(rx);
    rx->ring_head = rx->ring_count = 0;
    feld_recogniser_reset(rx->rec);
}

static void rx_free(ffeldhell_rx *rx)
{
    fdsp_fir_free(&rx->hilbert);
    fdsp_fftfilt_free(&rx->lpfilt);
    fdsp_movavgh_free(&rx->average);
    free(rx->ring);
    feld_recogniser_destroy(rx->rec);
    free(rx);
}

ffeldhell_rx *ffeldhell_rx_create(const ffeldhell_rx_config *cfg)
{
    ffeldhell_rx *rx;
    unsigned mask;
    double floor_;
    if (!cfg)
        return NULL;
    if (cfg->height < FFELDHELL_MIN_HEIGHT || cfg->height > FFELDHELL_MAX_HEIGHT ||
        cfg->agc < 1 || cfg->agc > 3 || !(cfg->rx_bw_hz >= 0.0) || cfg->rx_bw_hz > 4000.0 ||
        !(cfg->floor >= 0.0) || cfg->floor > 1.0 || !isfinite(cfg->squelch))
        return NULL;
    mask = cfg->font_mask ? cfg->font_mask : FELD_DEFAULT_FONT_MASK;
    if (mask >> FFELDHELL_FONT_COUNT)
        return NULL;
    floor_ = cfg->floor > 0.0 ? cfg->floor : FELD_DEFAULT_FLOOR;
    rx = (ffeldhell_rx *)calloc(1, sizeof *rx);
    if (!rx)
        return NULL;
    if (feld_params_init(&rx->p, cfg->submode) != 0 || !feld_carrier_ok(&rx->p, cfg->carrier_hz)) {
        free(rx);
        return NULL;
    }
    rx->footprint = sizeof *rx;
    rx->frequency = cfg->carrier_hz;
    rx->rx_bw = cfg->rx_bw_hz > 0.0 ? cfg->rx_bw_hz : rx->p.sub->rx_bw_default;
    rx->RxColumnLen = cfg->height;
    rx->hellagc = cfg->agc;
    rx->blackboard = cfg->blackboard != 0;
    rx->reverse = cfg->reverse != 0;
    rx->squelch_on = cfg->squelch_on != 0;
    rx->squelch = cfg->squelch;
    /* restart, feld.cxx:236: average->setLength(500 * samplerate / rxpixrate) (quirk Q7) */
    rx->average_len = (int)(500 * FELD_SAMPLE_RATE / (rx->RxColumnLen * rx->p.feldcolumnrate));
    rx->ring = (unsigned char *)malloc((size_t)RASTER_COLUMNS * (size_t)rx->RxColumnLen);
    rx->rec = NULL;
    if (!rx->ring || fdsp_fir_init_hilbert(&rx->hilbert, 37, 1) != 0 ||
        fdsp_fftfilt_init(&rx->lpfilt, 0, 0.5, FFT_LEN) != 0 ||
        fdsp_movavgh_init(&rx->average, rx->average_len) != 0 ||
        fdsp_movavg_init(&rx->bbfilt, 8) != 0) {
        rx_free(rx);
        return NULL;
    }
    rx->rec = feld_recogniser_create(rx->RxColumnLen, mask, floor_, rx->blackboard);
    if (!rx->rec) {
        rx_free(rx);
        return NULL;
    }
    rx->footprint += (size_t)RASTER_COLUMNS * (size_t)rx->RxColumnLen +
                     2 * 37 * sizeof(double) + (size_t)rx->average_len * sizeof(double) +
                     (size_t)(5 * FFT_LEN + FFT_LEN / 2) * sizeof(fdsp_cmpx) +
                     feld_recogniser_footprint(rx->rec);
    clear_state(rx);
    return rx;
}

void ffeldhell_rx_destroy(ffeldhell_rx *rx)
{
    if (rx)
        rx_free(rx);
}

void ffeldhell_rx_reset(ffeldhell_rx *rx)
{
    if (!rx)
        return;
    clear_state(rx);
    rx->dropped = 0;
    rx->ring_dropped = 0;
}

static fdsp_cmpx cmul(fdsp_cmpx a, fdsp_cmpx b)
{
    /* std::complex<double> operator* (four products in separate statements, never fused) */
    fdsp_cmpx r;
    double ac = a.re * b.re, bd = a.im * b.im, ad = a.re * b.im, bc = a.im * b.re;
    r.re = ac - bd;
    r.im = ad + bc;
    return r;
}

static double cabs_(fdsp_cmpx z)
{
    /* std::abs(std::complex<double>) = hypot */
    return hypot(z.re, z.im);
}

static double clamp(double x, double low, double high)
{
    /* CLAMP, util.h:52 (a NaN passes through) */
    return ((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x));
}

static int to_int(double v)
{
    /* the double -> int conversion of feld::rx (ix = CLAMP(...)): fldigi's reference build
     * (arm64) turns the 0/0 = NaN of a silent start into 0 (quirk Q11) */
    return v != v ? 0 : (int)v;
}

static fdsp_cmpx mixer(ffeldhell_rx *rx, fdsp_cmpx in)
{
    /* mixer, feld.cxx:296-310 */
    fdsp_cmpx z;
    double f = rx->frequency;
    fdsp_sincos(rx->rxphacc, &z.im, &z.re);
    z = cmul(z, in);
    rx->rxphacc -= 2.0 * M_PI * f / FELD_SAMPLE_RATE;
    if (rx->rxphacc < 0)
        rx->rxphacc += FELD_TWOPI;
    return z;
}

static void put_char(ffeldhell_rx *rx, const ffeldhell_char *c)
{
    if (rx->n_out < rx->max_out)
        rx->out[rx->n_out++] = *c;
    else
        rx->dropped++;
}

static void put_column(ffeldhell_rx *rx)
{
    /* REQ(put_rx_data, col_data, 2 * RxColumnLen) (feld.cxx:359-371, 427-439): the new
     * half of col_data is the displayed column; it goes to the raster ring once (fldigi
     * repeats it HellRcvWidth times for the display only) and to the recogniser */
    unsigned char *dst;
    int i, L = rx->RxColumnLen;
    ffeldhell_char buf[8];
    size_t n = 0, k;
    if (rx->ring_count == RASTER_COLUMNS) {
        rx->ring_dropped++;
        rx->ring_count--;
    }
    dst = rx->ring + ((rx->ring_head + rx->ring_count) % RASTER_COLUMNS) * (size_t)L;
    for (i = 0; i < L; i++)
        dst[i] = (unsigned char)(rx->col_data[L + i] & 0xFF);
    rx->ring_count++;
    feld_recogniser_column(rx->rec, rx->col_data + L, buf, 8, &n, &rx->dropped);
    for (k = 0; k < n; k++)
        put_char(rx, &buf[k]);
}

static void agc_decay(ffeldhell_rx *rx)
{
    /* the AGC of FSKH_rx and rx, feld.cxx:331-344, 403-416 (quirk Q2) */
    switch (rx->hellagc) {
    case 3:
        rx->agc *= (1.0 - 0.2 / rx->RxColumnLen);
        break;
    case 2:
        rx->agc *= (1.0 - 0.075 / rx->RxColumnLen);
        break;
    case 1:
    default:
        rx->agc *= (1.0 - 0.01 / rx->RxColumnLen);
    }
}

static void end_column(ffeldhell_rx *rx)
{
    /* column complete, feld.cxx:357-375, 425-443: displayed unless squelched (Q3), then
     * the new half becomes the old half */
    int i, L = rx->RxColumnLen;
    rx->col_pointer++;
    if (rx->col_pointer >= L) {
        if (rx->metric > rx->squelch || !rx->squelch_on)
            put_column(rx);
        rx->col_pointer = 0;
        for (i = 0; i < L; i++)
            rx->col_data[i] = rx->col_data[i + L];
    }
}

static void fskh_rx(ffeldhell_rx *rx, fdsp_cmpx z)
{
    /* FSKH_rx, feld.cxx:312-376: discriminator, smoothing, AGC, column (Q4, Q5) */
    double f, vid, avg;
    fdsp_cmpx cp;
    cp.re = rx->prev.re;
    cp.im = -rx->prev.im;
    cp = cmul(cp, z);
    f = atan2(cp.im, cp.re) * rx->p.phi2freq;
    rx->prev = z;
    f = fdsp_movavg_run(&rx->bbfilt, f);

    avg = fdsp_movavgh_run(&rx->average, cabs_(z));

    rx->rxcounter += rx->downsampleinc;
    if (rx->rxcounter < 1.0)
        return;
    rx->rxcounter -= 1.0;

    if (avg > rx->agc)
        rx->agc = avg;
    else
        agc_decay(rx);

    rx->metric = clamp(1000 * rx->agc, 0.0, 100.0);

    vid = clamp(0.5 * (f + 1), 0.0, 1.0);

    if (rx->reverse)
        vid = 1.0 - vid;
    if (rx->blackboard)
        vid = 1.0 - vid;

    rx->col_data[rx->col_pointer + rx->RxColumnLen] = (int)(vid * 255.0);
    end_column(rx);
}

static void am_rx(ffeldhell_rx *rx, fdsp_cmpx z)
{
    /* rx, feld.cxx:378-445: envelope, peak per pixel, peak hold (Q1), AGC (Q2), polarity
     * (Q4) */
    double x, avg;
    int ix;

    x = cabs_(z);
    if (x > rx->peakval)
        rx->peakval = x;
    avg = fdsp_movavgh_run(&rx->average, x);

    rx->rxcounter += rx->downsampleinc;
    if (rx->rxcounter < 1.0)
        return;

    rx->rxcounter -= 1.0;

    x = rx->peakval;
    rx->peakval = 0;
    if (x > rx->peakhold)
        rx->peakhold = x;
    else
        rx->peakhold *= (1.0 - 0.02 / rx->RxColumnLen);
    ix = to_int(clamp(255 * x / rx->peakhold, 0, 255));

    if (avg > rx->agc)
        rx->agc = avg;
    else
        agc_decay(rx);

    rx->metric = clamp(1000 * rx->agc, 0.0, 100.0);

    if (!rx->blackboard)
        ix = 255 - ix;

    rx->col_data[rx->col_pointer + rx->RxColumnLen] = ix;
    end_column(rx);
}

size_t ffeldhell_rx_process(ffeldhell_rx *rx, const double *x, size_t n, ffeldhell_char *out,
                            size_t max)
{
    /* rx_process, feld.cxx:447-510: analytic signal (x, x), Hilbert, mixer, low pass,
     * then FSKH_rx for the FM modes and rx for the AM modes */
    fdsp_cmpx z;
    const fdsp_cmpx *zp;
    size_t k;
    int i, m;
    if (!rx || (!x && n))
        return 0;
    rx->out = out;
    rx->max_out = out ? max : 0;
    rx->n_out = 0;
    for (k = 0; k < n; k++) {
        z.re = x[k];
        z.im = x[k];
        fdsp_fir_run(&rx->hilbert, z, &z);
        z = mixer(rx, z);
        m = fdsp_fftfilt_run(&rx->lpfilt, z, &zp);
        if (rx->p.fm) {
            for (i = 0; i < m; i++)
                fskh_rx(rx, zp[i]);
        } else {
            for (i = 0; i < m; i++)
                am_rx(rx, zp[i]);
        }
    }
    return rx->n_out;
}

size_t ffeldhell_rx_flush(ffeldhell_rx *rx, ffeldhell_char *out, size_t max)
{
    size_t n = 0;
    if (!rx)
        return 0;
    feld_recogniser_flush(rx->rec, out, out ? max : 0, &n, &rx->dropped);
    return n;
}

size_t ffeldhell_rx_take_columns(ffeldhell_rx *rx, unsigned char *out, size_t max_columns)
{
    size_t k, L;
    if (!rx || !out)
        return 0;
    L = (size_t)rx->RxColumnLen;
    for (k = 0; k < max_columns && rx->ring_count > 0; k++) {
        memcpy(out + k * L, rx->ring + rx->ring_head * L, L);
        rx->ring_head = (rx->ring_head + 1) % RASTER_COLUMNS;
        rx->ring_count--;
    }
    return k;
}

size_t ffeldhell_rx_columns_dropped(const ffeldhell_rx *rx)
{
    return rx ? rx->ring_dropped : 0;
}

int ffeldhell_rx_height(const ffeldhell_rx *rx)
{
    return rx ? rx->RxColumnLen : 0;
}

double ffeldhell_rx_metric(const ffeldhell_rx *rx)
{
    return rx ? rx->metric : 0.0;
}

double ffeldhell_rx_carrier_hz(const ffeldhell_rx *rx)
{
    return rx ? rx->frequency : 0.0;
}

int ffeldhell_rx_set_carrier(ffeldhell_rx *rx, double carrier_hz)
{
    if (!rx || !feld_carrier_ok(&rx->p, carrier_hz))
        return -1;
    rx->frequency = carrier_hz;
    return 0;
}

void ffeldhell_rx_set_reverse(ffeldhell_rx *rx, int on)
{
    if (rx)
        rx->reverse = on != 0;
}

void ffeldhell_rx_set_squelch(ffeldhell_rx *rx, int on, double level)
{
    if (!rx)
        return;
    rx->squelch_on = on != 0;
    if (isfinite(level))
        rx->squelch = level;
}

size_t ffeldhell_rx_dropped(const ffeldhell_rx *rx)
{
    return rx ? rx->dropped : 0;
}

size_t ffeldhell_rx_footprint(const ffeldhell_rx *rx)
{
    return rx ? rx->footprint : 0;
}
