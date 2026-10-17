/*
 * fldigi-feldhell-core internals: submode parameters and fonts shared by the receiver,
 * the recogniser and the transmitter (fldigi 4.2.13 src/feld/feld.cxx, src/include/feld.h,
 * src/include/fontdef.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FELDHELL_INTERNAL_H
#define FELDHELL_INTERNAL_H

#include "fldigi_feldhell.h"
#include "fldigi_dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define FELD_TWOPI (2.0 * M_PI)          /* TWOPI, misc.h */

#define FELD_SAMPLE_RATE 8000            /* FeldSampleRate, feld.h:37 */
#define FELD_MAXLEN 512                  /* MAXLEN (key shape length), feld.h:40 */
#define FELD_COLUMN_LEN 14               /* FELD_COLUMN_LEN, feld.h:42 */
#define FELD_MAX_RX_COLUMN_LEN 42        /* MAX_RX_COLUMN_LEN, feld.h:43 */
/* fldigi's waterfall limits LowFreqCutoff 0 / HighFreqCutoff 3000 (configuration.h) bound
 * the carrier in modem::set_freq (modem.cxx:319-324). */
#define FELD_LOW_CUTOFF 0.0
#define FELD_HIGH_CUTOFF 3000.0
/* Widest glyph: get_font_data answers columns 0..15 (feld.cxx:529). */
#define FELD_MAX_GLYPH_COLS 16

/* One raster font: 95 glyphs (' ' .. '~'), 14 rows of 16 bits, bit 15 = column 0
 * (struct fntchr, fontdef.h:33). */
typedef struct feldhell_font {
    const char *name;
    const unsigned short (*rows)[14];
} feldhell_font;

extern const feldhell_font FELDHELL_FONTS[FFELDHELL_FONT_COUNT];

/* One submode's modem parameters as feld::restart sets them (feld.cxx:146-259). */
typedef struct feld_params {
    const ffeldhell_submode *sub;
    int fm;
    int hard;                       /* HELLX5, HELLX9: hard keying (feld.cxx:592-594) */
    double feldcolumnrate;
    double txpixrate;               /* TxColumnLen * feldcolumnrate */
    double upsampleinc;             /* txpixrate / samplerate */
    double hell_bandwidth;          /* set_bandwidth(hell_bandwidth) */
    double filter_bandwidth;
    double phi2freq;                /* FM only */
} feld_params;

int feld_params_init(feld_params *p, const char *submode);
int feld_carrier_ok(const feld_params *p, double freq);
/* feld::get_font_data (feld.cxx:521-561) for one font: column bits, or -1 at the end. */
int feld_font_data(int font, unsigned char c, int col);
/* Width of a glyph: the number of columns get_font_data answers before -1. */
int feld_glyph_width(int font, unsigned char c);
/* The recogniser (feldhell_decode.c, new code, not an fldigi port). ink_high: 1 when ink
 * is the bright value (fldigi's blackboard), 0 when ink is dark (fldigi's default). */
typedef struct feld_recogniser feld_recogniser;
feld_recogniser *feld_recogniser_create(int height, unsigned font_mask, double floor,
                                        int ink_high);
void feld_recogniser_destroy(feld_recogniser *r);
size_t feld_recogniser_footprint(const feld_recogniser *r);
void feld_recogniser_reset(feld_recogniser *r);
/* One displayed column (height pixel values); appends recognised characters to out. */
void feld_recogniser_column(feld_recogniser *r, const int *column, ffeldhell_char *out,
                            size_t max, size_t *n, size_t *dropped);
void feld_recogniser_flush(feld_recogniser *r, ffeldhell_char *out, size_t max, size_t *n,
                           size_t *dropped);

#define FELD_DEFAULT_FONT_MASK ((1u << 0) | (1u << 4))
#define FELD_DEFAULT_FLOOR 0.55

#endif
