/*
 * fldigi-feldhell-core -- the Hellschreiber modem of fldigi 4.2.13 (Feld Hell, Slow Hell,
 * Feld Hell X5 and X9, FSK Hell-245, FSK Hell-105, Hell 80), receiver and transmitter,
 * ported to C99, plus a text recogniser for the received raster that fldigi does not have.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 * Ported from fldigi 4.2.13 (https://github.com/wave-owl/fldigi, tag v4.2.13, commit
 * e7c3ab3709ad62a7d91829364eb48a05fa325c0d): src/feld/feld.cxx, src/include/feld.h,
 * src/include/fontdef.h and the raster fonts src/feld/Feld*.cxx; (c) 2006-2014 Dave Freese
 * W1HKJ, with much owed to Joe Veldhuis N8FQ, adapted from gmfsk (c) 2001-2004 Tomi
 * Manninen OH2BNS and Lawrence Glaister VE7IT.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this
 * program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef FLDIGI_FELDHELL_H
#define FLDIGI_FELDHELL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FFELDHELL_VERSION_STRING "0.1.0"
#define FFELDHELL_ABI 1
/* The modem rate of every submode (FeldSampleRate, feld.h:37). */
#define FFELDHELL_RATE 8000
/* Pixels of one transmitted column (FELD_COLUMN_LEN, feld.h:42). */
#define FFELDHELL_TX_COLUMN 14
/* Receive column height range (HellRcvHeight slider 14..42, MAX_RX_COLUMN_LEN feld.h:43). */
#define FFELDHELL_MIN_HEIGHT 14
#define FFELDHELL_MAX_HEIGHT 42
/* Number of raster fonts (get_font_data's switch). */
#define FFELDHELL_FONT_COUNT 15

/* "fldigi-feldhell-core 0.1.0 contract=feldhell-c1 abi=1 tx=1 dsp=0.5.0". Static string.
 * The contract tag changes only with an incompatible API change. */
const char *ffeldhell_version(void);
int ffeldhell_abi(void);                /* FFELDHELL_ABI */

/* ------------------------------------------------------------------------------------
 * Submodes: every Hellschreiber mode of fldigi 4.2.13 (globals/globals.cxx:93-99,
 * parameters of feld::restart, feld.cxx:146-259).
 * ------------------------------------------------------------------------------------ */
typedef struct ffeldhell_submode {
    const char *name;        /* "FELDHELL" ... "HELL80" (fldigi's short name) */
    const char *label;       /* "Feld Hell" ... (fldigi's long name) */
    int fldigi_id;           /* trx_mode id of fldigi 4.2.13 (index into mode_info[]) */
    int fm;                  /* 1: frequency-shift family (FSKH245, FSKH105, HELL80) */
    double column_rate;      /* feldcolumnrate, columns per second */
    double hell_bandwidth;   /* fldigi's bandwidth (set_bandwidth); FM: the tone shift */
    double filter_bandwidth; /* restart's filter_bandwidth: 5 round(k bw / 5), k 1.2 / 4.0 */
    double rx_bw_default;    /* fldigi's per-mode receive bandwidth default HELL_BW_* */
    double baud;             /* 0.5 * txpixrate */
} ffeldhell_submode;

int ffeldhell_submode_count(void);                          /* 7 */
const ffeldhell_submode *ffeldhell_submode_at(int index);   /* NULL outside 0 .. count-1 */
/* Case-insensitive lookup by name; NULL when unknown. */
const ffeldhell_submode *ffeldhell_submode_find(const char *name);

/* ------------------------------------------------------------------------------------
 * Fonts: the 15 raster fonts of fldigi (0 "7x7" ... 14 "wide"; fldigi's default TX font is
 * 4, "hell"), ported verbatim.
 * ------------------------------------------------------------------------------------ */
int ffeldhell_font_count(void);                  /* FFELDHELL_FONT_COUNT */
const char *ffeldhell_font_name(int font);       /* NULL outside 0 .. count-1 */
int ffeldhell_font_find(const char *name);       /* index, -1 when unknown */
/* The transmitted columns of character c in a font, as feld::get_font_data returns them
 * column after column until -1 (feld.cxx:521-561): bit r = row r, row 0 sent first. Writes
 * at most max columns, returns their number (0 for characters outside ' ' .. '~' and for
 * an empty glyph such as space), -1 for an unknown font. */
int ffeldhell_font_columns(int font, unsigned char c, int *columns, int max);

/* ------------------------------------------------------------------------------------
 * Receiver: feld::rx_process (feld.cxx:447-510) with feld::rx (AM, 378-445) or
 * feld::FSKH_rx (FM, 312-376) builds fldigi's raster, column after column, exactly as
 * fldigi hands it to put_rx_data. On top of it -- NOT an fldigi port -- a recogniser
 * matches the raster against the glyphs of one or more fonts and reports characters with
 * a confidence in [0, 1]; below the confidence floor it reports "no match" (c = 0), never
 * a guess.
 * ------------------------------------------------------------------------------------ */
typedef struct ffeldhell_rx_config {
    const char *submode;     /* see ffeldhell_submode_find */
    double carrier_hz;       /* fldigi's frequency, 1500 */
    double rx_bw_hz;         /* fldigi's HELL_BW, the "Receive filter bandwidth" (10..2400);
                              * 0: the submode's rx_bw_default */
    int height;              /* HellRcvHeight, pixels per column, 14..42 (fldigi 20) */
    int agc;                 /* hellagc: 1 slow, 2 medium (fldigi), 3 fast */
    int blackboard;          /* HellBlackboard: reverse video */
    int reverse;             /* FM submodes: mirrored shift (fldigi's REV); AM ignores it */
    int squelch_on;          /* progStatus.sqlonoff (off in fldigi's bench) */
    double squelch;          /* sldrSquelchValue, metric units 0..100 (fldigi 5.0) */
    unsigned font_mask;      /* recogniser fonts, bit f = font f; 0: default (7x7 + hell) */
    double floor;            /* recogniser confidence floor; 0: default 0.55 */
} ffeldhell_rx_config;

/* fldigi's defaults for a submode (carrier 1500, rx_bw 0, height 20, agc 2, squelch off at
 * 5.0, recogniser fonts 7x7 and hell, floor 0.55). */
void ffeldhell_rx_config_default(ffeldhell_rx_config *cfg, const char *submode);

/* One recognised character. c: ASCII glyph, ' ' for a word gap, 0 for a block of ink
 * that matched no glyph above the floor. confidence: the match score in [0, 1] (1 for a
 * word gap). */
typedef struct ffeldhell_char {
    int c;
    double confidence;
} ffeldhell_char;

typedef struct ffeldhell_rx ffeldhell_rx;

/* NULL for an unknown submode, a carrier outside fldigi's range (bandwidth/2 ..
 * 3000 Hz - bandwidth/2, modem::set_freq), a bad parameter or out of memory. */
ffeldhell_rx *ffeldhell_rx_create(const ffeldhell_rx_config *cfg);
void ffeldhell_rx_destroy(ffeldhell_rx *rx);
/* Back to the state after create (filters, AGC, raster and recogniser cleared). */
void ffeldhell_rx_reset(ffeldhell_rx *rx);
/* Feed n samples at 8000 Hz (fldigi's sound card scale: int16 / 32768). Recognised
 * characters go to out (at most max; more are dropped and counted, out may be NULL with
 * max 0); returns their number. */
size_t ffeldhell_rx_process(ffeldhell_rx *rx, const double *x, size_t n, ffeldhell_char *out,
                            size_t max);
/* End of input: recognise what is still buffered as if blank columns followed. */
size_t ffeldhell_rx_flush(ffeldhell_rx *rx, ffeldhell_char *out, size_t max);
/* Raster: the columns fldigi would display since the last call, oldest first, each
 * `height` bytes (the pixel values of col_data, 0..255, polarity as fldigi draws them:
 * ink dark unless blackboard). The receiver keeps the last 4096 columns; older ones are
 * dropped and counted. Writes at most max_columns, returns their number. */
size_t ffeldhell_rx_take_columns(ffeldhell_rx *rx, unsigned char *out, size_t max_columns);
size_t ffeldhell_rx_columns_dropped(const ffeldhell_rx *rx);
int ffeldhell_rx_height(const ffeldhell_rx *rx);
/* fldigi's metric: clamp(1000 * agc, 0, 100), agc following the averaged magnitude. */
double ffeldhell_rx_metric(const ffeldhell_rx *rx);
double ffeldhell_rx_carrier_hz(const ffeldhell_rx *rx);
/* Retune; the carrier must be in range (as create). Returns 0 or -1. */
int ffeldhell_rx_set_carrier(ffeldhell_rx *rx, double carrier_hz);
void ffeldhell_rx_set_reverse(ffeldhell_rx *rx, int on);
void ffeldhell_rx_set_squelch(ffeldhell_rx *rx, int on, double level);
size_t ffeldhell_rx_dropped(const ffeldhell_rx *rx);
/* Bytes the receiver allocated (all at create). */
size_t ffeldhell_rx_footprint(const ffeldhell_rx *rx);

/* Recogniser alone (the same code the receiver runs): match one block of `height`-pixel
 * columns (values as ffeldhell_rx_take_columns returns them, normal polarity) against the
 * glyphs of the fonts in font_mask. Writes the recognised characters, returns their
 * number; never fails on input that matches nothing. */
size_t ffeldhell_recognise(const unsigned char *columns, size_t n_columns, int height,
                           unsigned font_mask, double floor, ffeldhell_char *out, size_t max);

/* ------------------------------------------------------------------------------------
 * Transmitter: feld::tx_process (feld.cxx:661-715) step by step. Output: the samples
 * fldigi hands to modem::ModulateXmtr (peak 1.0), before the TX level (multiply by
 * 10^(txlevel/20), fldigi's -12 dB, to reproduce fldigi's WAV). fldigi sets no soft start
 * or stop for Hellschreiber.
 * ------------------------------------------------------------------------------------ */
typedef struct ffeldhell_tx_config {
    const char *submode;
    double carrier_hz;       /* fldigi's tx frequency, range as the receiver */
    int reverse;             /* FM submodes: mirrored shift */
    int font;                /* feldfontnbr 0..14 (fldigi 4, "hell") */
    int pulse;               /* HellPulseFast 0 4 ms, 1 2 ms (fldigi), 2 1 ms, 3 hard */
    int width;               /* HellXmtWidth 1..3 (fldigi 1) */
    int idle_dots;           /* HellXmtIdle: '.' while idle instead of 2 blank columns */
} ffeldhell_tx_config;

/* fldigi's defaults (carrier 1500, font 4, pulse 1, width 1, no idle dots). */
void ffeldhell_tx_config_default(ffeldhell_tx_config *cfg, const char *submode);

typedef struct ffeldhell_tx ffeldhell_tx;

ffeldhell_tx *ffeldhell_tx_create(const ffeldhell_tx_config *cfg);   /* NULL as rx_create */
void ffeldhell_tx_destroy(ffeldhell_tx *tx);
/* The largest output of one call (ffeldhell_tx_end: four characters). */
size_t ffeldhell_tx_bound(const ffeldhell_tx *tx);
/* Start of an over (tx_init and the preamble, feld.cxx:59-67, 672-678): three '.'.
 * Each call writes all or nothing: 0 when max is below ffeldhell_tx_bound. */
size_t ffeldhell_tx_begin(ffeldhell_tx *tx, double *out, size_t max);
/* One keyboard character (tx_char, feld.cxx:633-659): CR and LF are sent as space,
 * characters outside ' ' .. '~' as two blank columns. */
size_t ffeldhell_tx_put(ffeldhell_tx *tx, unsigned char c, double *out, size_t max);
/* One idle step with an empty TX buffer (feld.cxx:700-707). */
size_t ffeldhell_tx_idle(ffeldhell_tx *tx, double *out, size_t max);
/* End of an over (postamble, feld.cxx:680-695): three '.' and a space. */
size_t ffeldhell_tx_end(ffeldhell_tx *tx, double *out, size_t max);
/* Columns (14 pixels each) a character takes, framing included, without sending it. */
int ffeldhell_tx_columns(const ffeldhell_tx *tx, unsigned char c);
/* Samples of n columns at this submode's rate (the exact count depends on the phase of
 * fldigi's fractional pixel counter; this is the rounded mean). */
double ffeldhell_tx_samples_per_column(const ffeldhell_tx *tx);

#ifdef __cplusplus
}
#endif

#endif
