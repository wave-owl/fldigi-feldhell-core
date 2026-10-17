/*
 * fldigi-feldhell-core transmitter: feld::tx_init, nco, send_symbol, send_null_column,
 * tx_char, tx_process and initKeyWaveform (fldigi 4.2.13 src/feld/feld.cxx:59-67,
 * 563-741).
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

#define OUTBUFSIZE 65536            /* modem.h:37 */
#define PREAMBLE 3                  /* tx_init, feld.cxx:63 */
#define POSTAMBLE 3                 /* tx_process, feld.cxx:694 */

struct ffeldhell_tx {
    feld_params p;
    double frequency;               /* tx_frequency */
    int reverse;
    int font;
    int hardkeying;                 /* HellPulseFast */
    int width;                      /* HellXmtWidth */
    int idle_dots;                  /* HellXmtIdle */
    double txphacc;
    double txcounter;
    int prevsymb;
    double OnShape[FELD_MAXLEN];
    double OffShape[FELD_MAXLEN];
    /* output of the current call */
    double *out;
    size_t n_out;
};

void ffeldhell_tx_config_default(ffeldhell_tx_config *cfg, const char *submode)
{
    /* fldigi's defaults: feldfontnbr 4, HellPulseFast 1, HellXmtWidth 1, HellXmtIdle off
     * (configuration.h:755-767, 1043-1045) */
    if (!cfg)
        return;
    memset(cfg, 0, sizeof *cfg);
    cfg->submode = submode;
    cfg->carrier_hz = 1500.0;
    cfg->font = 4;
    cfg->pulse = 1;
    cfg->width = 1;
}

static void init_key_waveform(ffeldhell_tx *tx)
{
    /* initKeyWaveform, feld.cxx:717-741: raised-cosine key-down edge over 32 (slow), 16
     * (medium) or 8 (fast) samples, or a hard edge; the key-up edge is its mirror inside
     * the first 32 samples (quirk Q12: fast falls through to hard for i >= 8) */
    int i;
    for (i = 0; i < FELD_MAXLEN; i++) {
        tx->OnShape[i] = 1.0;
        tx->OffShape[i] = 0.0;
    }
    for (i = 0; i < 32; i++) {
        switch (tx->hardkeying) {
        case 0:
            tx->OnShape[i] = 0.5 * (1.0 - cos(M_PI * i / 33));
            break;
        case 1:
            if (i < 16)
                tx->OnShape[i] = 0.5 * (1.0 - cos(M_PI * i / 16));
            break;
        case 2:
            if (i < 8)
                tx->OnShape[i] = 0.5 * (1.0 - cos(M_PI * i / 8));
            break;
        case 3:
        default:
            break;
        }
        tx->OffShape[31 - i] = tx->OnShape[i];
    }
}

ffeldhell_tx *ffeldhell_tx_create(const ffeldhell_tx_config *cfg)
{
    /* constructor (feld.cxx:268-292: txphacc 0) and restart (feld.cxx:146-259) */
    ffeldhell_tx *tx;
    if (!cfg || cfg->font < 0 || cfg->font >= FFELDHELL_FONT_COUNT || cfg->pulse < 0 ||
        cfg->pulse > 3 || cfg->width < 1 || cfg->width > 3)
        return NULL;
    tx = (ffeldhell_tx *)calloc(1, sizeof *tx);
    if (!tx)
        return NULL;
    if (feld_params_init(&tx->p, cfg->submode) != 0 || !feld_carrier_ok(&tx->p, cfg->carrier_hz)) {
        free(tx);
        return NULL;
    }
    tx->frequency = cfg->carrier_hz;
    tx->reverse = cfg->reverse != 0;
    tx->font = cfg->font;
    tx->hardkeying = cfg->pulse;
    tx->width = cfg->width;
    tx->idle_dots = cfg->idle_dots != 0;
    tx->txphacc = 0.0;
    init_key_waveform(tx);
    return tx;
}

void ffeldhell_tx_destroy(ffeldhell_tx *tx)
{
    free(tx);
}

static double nco(ffeldhell_tx *tx, double freq)
{
    /* nco, feld.cxx:563-572 */
    double x = sin(tx->txphacc);

    tx->txphacc += 2.0 * M_PI * freq / FELD_SAMPLE_RATE;

    if (tx->txphacc > FELD_TWOPI)
        tx->txphacc -= FELD_TWOPI;

    return x;
}

static void send_symbol(ffeldhell_tx *tx, int currsymb, int nextsymb)
{
    /* send_symbol, feld.cxx:574-625: one pixel; FM shifts the tone by -+ bandwidth / 2
     * (reverse mirrors it, Q5), X5/X9 key hard, Feld/Slow Hell shape the edges with
     * OnShape/OffShape; the pixel ends when the fractional counter wraps. The local echo
     * into rx_process (feld.cxx:623) is the GUI's and not reproduced. */
    int outlen = 0;
    double Amp = 1.0;
    double tone = tx->frequency;    /* get_txfreq_woffset, TxOffset 0 */
    double *outbuf = tx->out + tx->n_out;
    int mode_fm = tx->p.fm, hard = tx->p.hard;

    if (mode_fm)
        tone += (tx->reverse ? -1 : 1) * (currsymb ? -1 : 1) * tx->p.hell_bandwidth / 2.0;
    for (;;) {
        if (mode_fm) {
            /* FSKH245, FSKH105, HELL80: constant amplitude */
        } else if (hard) {
            Amp = currsymb;             /* HELLX5, HELLX9 */
        } else {                        /* FELDHELL, SLOWHELL */
            if (tx->prevsymb == 0 && currsymb == 1) {
                Amp = tx->OnShape[outlen];
            } else if (currsymb == 1 && nextsymb == 0) {
                Amp = tx->OffShape[outlen];
            } else
                Amp = currsymb;
        }
        outbuf[outlen++] = Amp * nco(tx, tone);

        if (outlen >= OUTBUFSIZE) {
            outlen = 0;
            break;
        }
        tx->txcounter += tx->p.upsampleinc;
        if (tx->txcounter < 1.0)
            continue;
        tx->txcounter -= 1.0;
        break;
    }
    tx->prevsymb = currsymb;
    tx->n_out += (size_t)outlen;
}

static void send_null_column(ffeldhell_tx *tx)
{
    /* send_null_column, feld.cxx:627-631 */
    int i;
    for (i = 0; i < 14; i++)
        send_symbol(tx, 0, 0);
}

static void tx_char(ffeldhell_tx *tx, unsigned char c)
{
    /* tx_char, feld.cxx:633-659: a blank column before and after every character, three
     * more for a space (Q8), each glyph column HellXmtWidth times (Q9), rows bottom up */
    int column = 0, bits, colbits, currbit, nextbit, col, i;
    send_null_column(tx);
    if (c == ' ') {
        send_null_column(tx);
        send_null_column(tx);
        send_null_column(tx);
    } else {
        while ((bits = feld_font_data(tx->font, c, column)) != -1) {
            for (col = 0; col < tx->width; col++) {
                colbits = bits;
                for (i = 0; i < 14; i++) {
                    currbit = colbits & 1;
                    colbits = colbits >> 1;
                    nextbit = colbits & 1;
                    send_symbol(tx, currbit, nextbit);
                }
            }
            column++;
        }
    }
    send_null_column(tx);
}

int ffeldhell_tx_columns(const ffeldhell_tx *tx, unsigned char c)
{
    /* the columns tx_char sends (feld.cxx:633-659) */
    if (!tx)
        return 0;
    if (c == '\r' || c == '\n')
        c = ' ';
    if (c == ' ')
        return 5;
    return 2 + feld_glyph_width(tx->font, c) * tx->width;
}

static size_t samples_per_pixel_max(const ffeldhell_tx *tx)
{
    return (size_t)ceil(1.0 / tx->p.upsampleinc) + 1;
}

size_t ffeldhell_tx_bound(const ffeldhell_tx *tx)
{
    int c, w = 0, cols;
    if (!tx)
        return 0;
    for (c = ' '; c <= '~'; c++) {
        cols = ffeldhell_tx_columns(tx, (unsigned char)c);
        if (cols > w)
            w = cols;
    }
    cols = PREAMBLE * ffeldhell_tx_columns(tx, '.') + 5;
    if (w > cols)
        cols = w;
    return (size_t)cols * 14 * samples_per_pixel_max(tx);
}

double ffeldhell_tx_samples_per_column(const ffeldhell_tx *tx)
{
    return tx ? 14.0 / tx->p.upsampleinc : 0.0;
}

static int start(ffeldhell_tx *tx, double *out, size_t max)
{
    if (!tx || !out || max < ffeldhell_tx_bound(tx))
        return 0;
    tx->out = out;
    tx->n_out = 0;
    return 1;
}

size_t ffeldhell_tx_begin(ffeldhell_tx *tx, double *out, size_t max)
{
    /* tx_init (feld.cxx:59-67: txcounter 0, prevsymb off, preamble 3) and the preamble of
     * tx_process (feld.cxx:672-678): three '.' */
    int i;
    if (!start(tx, out, max))
        return 0;
    tx->txcounter = 0.0;
    tx->prevsymb = 0;
    for (i = 0; i < PREAMBLE; i++)
        tx_char(tx, '.');
    return tx->n_out;
}

size_t ffeldhell_tx_put(ffeldhell_tx *tx, unsigned char c, double *out, size_t max)
{
    /* tx_process for one character, feld.cxx:709-712: CR and LF become a space */
    if (!start(tx, out, max))
        return 0;
    if (c == '\r' || c == '\n')
        c = ' ';
    tx_char(tx, c);
    return tx->n_out;
}

size_t ffeldhell_tx_idle(ffeldhell_tx *tx, double *out, size_t max)
{
    /* empty TX buffer, feld.cxx:700-707: '.' (HellXmtIdle) or two blank columns */
    if (!start(tx, out, max))
        return 0;
    if (tx->idle_dots) {
        tx_char(tx, '.');
    } else {
        send_null_column(tx);
        send_null_column(tx);
    }
    return tx->n_out;
}

size_t ffeldhell_tx_end(ffeldhell_tx *tx, double *out, size_t max)
{
    /* end of transmission, feld.cxx:680-695: postamble of three '.' and a space */
    int i;
    if (!start(tx, out, max))
        return 0;
    for (i = 0; i < POSTAMBLE; i++)
        tx_char(tx, '.');
    tx_char(tx, ' ');
    return tx->n_out;
}
