/*
 * fldigi-feldhell-core: version, submode table, fonts, get_font_data and the per-submode
 * parameters of fldigi 4.2.13's feld::restart.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain; ported from fldigi 4.2.13 src/feld/feld.cxx
 * (c) 2006-2014 Dave Freese W1HKJ, with much owed to Joe Veldhuis N8FQ, adapted from gmfsk.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "feldhell_internal.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#define STR2(x) #x
#define STR(x) STR2(x)

const char *ffeldhell_version(void)
{
    return "fldigi-feldhell-core " FFELDHELL_VERSION_STRING " contract=feldhell-c1 abi=" STR(
               FFELDHELL_ABI) " tx=1 dsp=" FDSP_VERSION_STRING;
}

int ffeldhell_abi(void)
{
    return FFELDHELL_ABI;
}

/* Submodes: globals/globals.cxx:93-99 (trx_mode ids 31-37 of fldigi 4.2.13); column rate,
 * bandwidth and filter of feld::restart (feld.cxx:146-259); the receive bandwidth
 * defaults HELL_BW_FH .. HELL_BW_HELL80 of configuration.h:736-742 (fldigi never reads
 * them back, see ffeldhell_rx_config.rx_bw_hz). The filter bandwidths are the values the
 * restart formula gives (checked in tests/test_feldhell.c). */
static const ffeldhell_submode SUBMODES[7] = {
    {"FELDHELL", "Feld Hell", 31, 0, 17.5, 245.0, 295.0, 245.0, 122.5},
    {"SLOWHELL", "Slow Hell", 32, 0, 2.1875, 30.625, 35.0, 30.0, 15.3125},
    {"HELLX5", "Feld Hell X5", 33, 0, 87.5, 1225.0, 1470.0, 1225.0, 612.5},
    {"HELLX9", "Feld Hell X9", 34, 0, 157.5, 2205.0, 2645.0, 2205.0, 1102.5},
    {"FSKH245", "FSK Hell-245", 35, 1, 17.5, 122.5, 490.0, 250.0, 122.5},
    {"FSKH105", "FSK Hell-105", 36, 1, 17.5, 55.0, 220.0, 120.0, 122.5},
    {"HELL80", "Hell 80", 37, 1, 35.0, 300.0, 1200.0, 450.0, 245.0},
};

int ffeldhell_submode_count(void)
{
    return 7;
}

const ffeldhell_submode *ffeldhell_submode_at(int index)
{
    return index >= 0 && index < 7 ? &SUBMODES[index] : NULL;
}

static int name_eq(const char *a, const char *b)
{
    size_t k;
    for (k = 0; a[k] && toupper((unsigned char)b[k]) == toupper((unsigned char)a[k]); k++)
        ;
    return !a[k] && !b[k];
}

const ffeldhell_submode *ffeldhell_submode_find(const char *name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < 7; i++)
        if (name_eq(SUBMODES[i].name, name))
            return &SUBMODES[i];
    return NULL;
}

int ffeldhell_font_count(void)
{
    return FFELDHELL_FONT_COUNT;
}

const char *ffeldhell_font_name(int font)
{
    return font >= 0 && font < FFELDHELL_FONT_COUNT ? FELDHELL_FONTS[font].name : NULL;
}

int ffeldhell_font_find(const char *name)
{
    int i;
    if (!name)
        return -1;
    for (i = 0; i < FFELDHELL_FONT_COUNT; i++)
        if (name_eq(FELDHELL_FONTS[i].name, name))
            return i;
    return -1;
}

int feld_font_data(int font, unsigned char c, int col)
{
    /* get_font_data, feld.cxx:521-561 (font chosen by number as its switch, 532-549) */
    int bits = 0, mask, bin, ordbits = 0, row, i, testval;
    const unsigned short (*rows)[14];
    if (col > 15 || c < ' ' || c > '~')
        return -1;
    if (font < 0 || font >= FFELDHELL_FONT_COUNT)
        font = 0;   /* default: feld7x7_14 */
    rows = FELDHELL_FONTS[font].rows;
    mask = 1 << (15 - col);
    for (i = 0; i < 14; i++)
        ordbits |= rows[c - ' '][i];
    for (row = 0; row < 14; row++) {
        bin = rows[c - ' '][13 - row] & mask;
        if (bin != 0)
            bits |= 1 << row;
    }
    testval = (1 << (15 - col)) - 1;
    if ((bits == 0) && ((ordbits & testval) == 0))
        return -1;
    return bits;
}

int feld_glyph_width(int font, unsigned char c)
{
    /* the column loop of tx_char, feld.cxx:644-655 */
    int w = 0;
    while (feld_font_data(font, c, w) != -1)
        w++;
    return w;
}

int ffeldhell_font_columns(int font, unsigned char c, int *columns, int max)
{
    /* get_font_data column after column until -1, as tx_char runs it (feld.cxx:644-655) */
    int col, bits;
    if (font < 0 || font >= FFELDHELL_FONT_COUNT)
        return -1;
    for (col = 0; (bits = feld_font_data(font, c, col)) != -1; col++)
        if (columns && col < max)
            columns[col] = bits;
    return col < max || !columns ? col : max;
}

int feld_params_init(feld_params *p, const char *submode)
{
    /* feld::restart, feld.cxx:146-259: TxColumnLen = FELD_COLUMN_LEN, the column rate of
     * the mode, txpixrate, upsampleinc, the bandwidth and the receive filter (AM 1.2 x,
     * FM 4.0 x the bandwidth, quirk Q6), phi2freq of the FM modes */
    const ffeldhell_submode *s = ffeldhell_submode_find(submode);
    if (!s)
        return -1;
    memset(p, 0, sizeof *p);
    p->sub = s;
    p->fm = s->fm;
    p->hard = s == &SUBMODES[2] || s == &SUBMODES[3];
    p->feldcolumnrate = s->column_rate;
    p->txpixrate = (FELD_COLUMN_LEN * p->feldcolumnrate);
    p->upsampleinc = (double)(p->txpixrate / FELD_SAMPLE_RATE);
    if (!p->fm) {
        p->hell_bandwidth = p->txpixrate;
        p->filter_bandwidth = 5 * round(1.2 * p->hell_bandwidth / 5.0);
    } else {
        p->hell_bandwidth = s->hell_bandwidth;
        p->phi2freq = FELD_SAMPLE_RATE / M_PI / (p->hell_bandwidth / 2.0);
        p->filter_bandwidth = 5 * round(4.0 * p->hell_bandwidth / 5.0);
    }
    return 0;
}

int feld_carrier_ok(const feld_params *p, double freq)
{
    /* modem::set_freq, modem.cxx:319-324: CLAMP(freq, Low + bw/2, High - bw/2) leaves the
     * carrier unchanged */
    double lo = FELD_LOW_CUTOFF + p->hell_bandwidth / 2;
    double hi = FELD_HIGH_CUTOFF - p->hell_bandwidth / 2;
    return isfinite(freq) && freq >= lo && freq <= hi;
}
