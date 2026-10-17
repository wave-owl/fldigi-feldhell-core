/*
 * fldigi-feldhell-core recogniser: received raster -> characters.
 *
 * NEW CODE, NOT AN FLDIGI PORT. fldigi 4.2.13 draws the Hellschreiber raster for a human
 * to read and has no code that turns it back into text. This file matches the raster the
 * ported receiver builds against the glyphs of the ported fonts (the same ones
 * get_font_data hands to the transmitter, so the templates are exactly what fldigi
 * sends) and reports a character only when the match is good enough:
 *
 * - The displayed columns are joined into one pixel stream (a column of `height` pixels
 *   spans one transmitted column of 14 pixels, so the stream is the received keying in
 *   time order; the vertical phase of the raster drops out).
 * - Each glyph of each selected font is rendered at the receive resolution with one blank
 *   column before and after it (tx_char brackets every character with blank columns).
 * - At the first ink after the previous character, every template is slid over +-1
 *   column around that point; the score is the Pearson correlation of template and
 *   stream, clipped to [0, 1]. The best fit is the character when its score reaches the
 *   floor, the glyph stands out from its surroundings (contrast CONTRAST_MIN) and the
 *   score is significant for the glyph's size (a thin glyph such as '!' correlates with
 *   noise by chance far more often than a letter, so it needs SIG_K / sqrt(ink pixels), at most SIG_MAX);
 *   otherwise the block is reported as "no match" (c = 0), never as a guess.
 * - A small isolated glyph ('.' and the apostrophe) fits the stream at several vertical
 *   positions. The column phase (where a transmitted column starts in the stream) is the
 *   same for the whole transmission, so it is learned from confident matches of tall
 *   glyphs (ink over at least 7 of the 14 rows, which no vertical shift can mimic; a decaying
 *   average on the circle) and, once known, only offsets within PHASE_TOL pixels of it
 *   are tried.
 * - A gap of at least 5 blank columns after a character is a word gap (tx_char sends a
 *   space as 3 blank columns inside its brackets: 7 between the glyphs, 2 otherwise).
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "feldhell_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define INK_ON 0.6          /* ink: clearly more than the mid grey of an empty FM channel */
#define SPACE_GAP 5         /* blank columns after a character that make a word gap */
#define CONTRAST_MIN 0.4   /* ink on the glyph minus ink around it, at least */
#define SIG_K 3.5           /* score >= SIG_K / sqrt(ink pixels): thin glyphs need more, */
#define SIG_MAX 0.9         /* but never more than this */
#define PHASE_SCORE 0.75    /* matches that teach the column phase */
#define PHASE_DECAY 0.7     /* weight of the previous phase estimate */
#define PHASE_LOCK 0.5      /* length of the averaged phase vector that counts as known */

enum { LAST_NONE, LAST_CHAR, LAST_OTHER };

typedef struct tmpl {
    int c;
    int width;              /* glyph columns */
    int len;                /* (width + 2) * height pixels */
    int lead;               /* first ink pixel */
    int nink;
    const int *ink;         /* ink pixel offsets */
    double var;             /* nink - nink^2 / len */
    int tall;               /* ink spans at least half the column: fixes the phase */
} tmpl;

struct feld_recogniser {
    int P;
    int ink_high;
    double floor;
    int wmax, maxlead;
    int ntmpl;
    tmpl *t;
    int *inkpool;
    double *s;              /* ink stream */
    double *ps, *ps2;       /* prefix sums of a search window */
    int cap, len, pos, last_end, last;
    double ph_re, ph_im;    /* averaged column phase on the unit circle */
    int phase_tol;
    size_t footprint;
};

static int glyph_bit(int font, int c, int col, int k, int P)
{
    /* template pixel k of glyph column col: transmitted row floor((k + 1/2) 14 / P) */
    int bits = feld_font_data(font, (unsigned char)c, col);
    int row = ((2 * k + 1) * FELD_COLUMN_LEN) / (2 * P);
    return bits > 0 && ((bits >> row) & 1);
}

feld_recogniser *feld_recogniser_create(int height, unsigned font_mask, double floor,
                                        int ink_high)
{
    feld_recogniser *r;
    int f, c, w, m, P = height, total = 0, nt = 0, k;
    r = (feld_recogniser *)calloc(1, sizeof *r);
    if (!r)
        return NULL;
    r->P = P;
    r->ink_high = ink_high != 0;
    r->floor = floor;
    r->phase_tol = P / 10 > 1 ? P / 10 : 1;
    for (f = 0; f < FFELDHELL_FONT_COUNT; f++) {
        if (!(font_mask & (1u << f)))
            continue;
        for (c = '!'; c <= '~'; c++) {
            w = feld_glyph_width(f, (unsigned char)c);
            if (w == 0)
                continue;
            nt++;
            total += w * P;
            if (w > r->wmax)
                r->wmax = w;
        }
    }
    if (nt == 0) {
        free(r);
        return NULL;
    }
    r->t = (tmpl *)calloc((size_t)nt, sizeof(tmpl));
    r->inkpool = (int *)malloc((size_t)total * sizeof(int));
    r->cap = (r->wmax + 8) * P * 4;
    r->s = (double *)calloc((size_t)r->cap, sizeof(double));
    r->ps = (double *)malloc((size_t)(r->cap + 1) * sizeof(double));
    r->ps2 = (double *)malloc((size_t)(r->cap + 1) * sizeof(double));
    if (!r->t || !r->inkpool || !r->s || !r->ps || !r->ps2) {
        feld_recogniser_destroy(r);
        return NULL;
    }
    total = 0;
    for (f = 0; f < FFELDHELL_FONT_COUNT; f++) {
        if (!(font_mask & (1u << f)))
            continue;
        for (c = '!'; c <= '~'; c++) {
            tmpl *t;
            w = feld_glyph_width(f, (unsigned char)c);
            if (w == 0)
                continue;
            t = &r->t[r->ntmpl];
            t->c = c;
            t->width = w;
            t->len = (w + 2) * P;
            t->ink = r->inkpool + total;
            t->lead = -1;
            for (m = 0; m < w * P; m++) {
                k = m % P;
                if (glyph_bit(f, c, m / P, k, P)) {
                    r->inkpool[total + t->nink++] = P + m;
                    if (t->lead < 0)
                        t->lead = P + m;
                }
            }
            if (t->nink == 0)
                continue;
            {
                int col, bits, lo_row = 14, hi_row = -1, row;
                for (col = 0; col < w; col++) {
                    bits = feld_font_data(f, (unsigned char)c, col);
                    for (row = 0; row < 14 && bits > 0; row++)
                        if ((bits >> row) & 1) {
                            if (row < lo_row)
                                lo_row = row;
                            if (row > hi_row)
                                hi_row = row;
                        }
                }
                t->tall = hi_row - lo_row + 1 >= 7;
            }
            total += t->nink;
            t->var = t->nink - (double)t->nink * t->nink / t->len;
            if (t->lead > r->maxlead)
                r->maxlead = t->lead;
            r->ntmpl++;
        }
    }
    r->footprint = sizeof *r + (size_t)nt * sizeof(tmpl) + (size_t)total * sizeof(int) +
                   (size_t)r->cap * sizeof(double) * 3;
    feld_recogniser_reset(r);
    return r;
}

void feld_recogniser_destroy(feld_recogniser *r)
{
    if (!r)
        return;
    free(r->t);
    free(r->inkpool);
    free(r->s);
    free(r->ps);
    free(r->ps2);
    free(r);
}

size_t feld_recogniser_footprint(const feld_recogniser *r)
{
    return r ? r->footprint : 0;
}

void feld_recogniser_reset(feld_recogniser *r)
{
    if (!r)
        return;
    r->len = 0;
    r->pos = 0;
    r->last_end = 0;
    r->last = LAST_NONE;
    r->ph_re = r->ph_im = 0.0;
}

static int phase_known(const feld_recogniser *r, int *phase)
{
    double a;
    if (hypot(r->ph_re, r->ph_im) < PHASE_LOCK)
        return 0;
    a = atan2(r->ph_im, r->ph_re);
    if (a < 0)
        a += 2.0 * M_PI;
    *phase = (int)floor(a * r->P / (2.0 * M_PI) + 0.5) % r->P;
    return 1;
}

static void learn_phase(feld_recogniser *r, int offset)
{
    double a = 2.0 * M_PI * (double)(((offset % r->P) + r->P) % r->P) / r->P;
    r->ph_re = PHASE_DECAY * r->ph_re + (1.0 - PHASE_DECAY) * cos(a);
    r->ph_im = PHASE_DECAY * r->ph_im + (1.0 - PHASE_DECAY) * sin(a);
}

static int phase_ok(const feld_recogniser *r, int known, int phase, int offset)
{
    int d;
    if (!known)
        return 1;
    d = (((offset - phase) % r->P) + r->P) % r->P;
    if (d > r->P / 2)
        d = r->P - d;
    return d <= r->phase_tol;
}

static void emit(int c, double conf, ffeldhell_char *out, size_t max, size_t *n,
                 size_t *dropped)
{
    if (*n < max) {
        out[*n].c = c;
        out[*n].confidence = conf;
        (*n)++;
    } else {
        (*dropped)++;
    }
}

static void compact(feld_recogniser *r)
{
    /* keep the history a search can reach back into (maxlead + 1 column before pos); move
     * by whole columns so that stream offsets keep their column phase */
    int keep = r->pos - r->maxlead - 2 * r->P, d;
    if (keep <= 0)
        return;
    d = keep - keep % r->P;
    if (d == 0)
        return;
    memmove(r->s, r->s + d, (size_t)(r->len - d) * sizeof(double));
    r->len -= d;
    r->pos -= d;
    r->last_end -= d;
}

/* Best template at the first ink q: returns the score, sets *best_t, *best_o and the
 * contrast (mean ink on the glyph minus mean ink around it) of that fit. */
static double best_match(feld_recogniser *r, int q, int *best_t, int *best_o, double *contrast)
{
    int P = r->P, lo, hi, i, j, o, o0, o1, phase = 0, known = phase_known(r, &phase);
    double best = -2.0;
    lo = q - r->maxlead - P;
    if (lo < 0)
        lo = 0;
    hi = q + (r->wmax + 3) * P;
    if (hi > r->len)
        hi = r->len;
    r->ps[0] = r->ps2[0] = 0.0;
    for (i = lo; i < hi; i++) {
        r->ps[i - lo + 1] = r->ps[i - lo] + r->s[i];
        r->ps2[i - lo + 1] = r->ps2[i - lo] + r->s[i] * r->s[i];
    }
    *best_t = -1;
    *best_o = q;
    *contrast = 0.0;
    for (j = 0; j < r->ntmpl; j++) {
        const tmpl *t = &r->t[j];
        o0 = q - t->lead - P;
        o1 = q - t->lead + P;
        for (o = o0; o <= o1; o++) {
            double n = t->len, sum, sum2, var, dot = 0.0, score;
            int a = o - lo;
            if (o < lo || o + t->len > hi || !phase_ok(r, known, phase, o))
                continue;
            sum = r->ps[a + t->len] - r->ps[a];
            sum2 = r->ps2[a + t->len] - r->ps2[a];
            var = sum2 - sum * sum / n;
            if (var <= 1e-9)
                continue;
            for (i = 0; i < t->nink; i++)
                dot += r->s[o + t->ink[i]];
            score = (dot - sum * t->nink / n) / sqrt(t->var * var);
            if (score > best) {
                best = score;
                *best_t = j;
                *best_o = o;
                *contrast = dot / t->nink - (sum - dot) / (n - t->nink);
            }
        }
    }
    return best < 0.0 ? 0.0 : (best > 1.0 ? 1.0 : best);
}

static void scan(feld_recogniser *r, int flushing, ffeldhell_char *out, size_t max,
                 size_t *n, size_t *dropped)
{
    int P = r->P, q, bt, bo;
    double score, contrast;
    for (;;) {
        for (q = r->pos; q < r->len && r->s[q] < INK_ON; q++)
            ;
        if (q >= r->len) {
            r->pos = r->len;
            return;
        }
        if (!flushing && r->len < q + (r->wmax + 3) * P)
            return;
        if (r->last == LAST_CHAR && q - r->last_end >= SPACE_GAP * P)
            emit(' ', 1.0, out, max, n, dropped);
        score = best_match(r, q, &bt, &bo, &contrast);
        if (bt >= 0 && score >= r->floor && contrast >= CONTRAST_MIN &&
            score >= fmin(SIG_MAX, SIG_K / sqrt((double)r->t[bt].nink))) {
            emit(r->t[bt].c, score, out, max, n, dropped);
            if (score >= PHASE_SCORE && r->t[bt].tall)
                learn_phase(r, bo);
            r->last_end = bo + (1 + r->t[bt].width) * P;
            r->pos = r->last_end + P;   /* skip the trailing blank column */
            r->last = LAST_CHAR;
        } else {
            emit(0, score, out, max, n, dropped);   /* no match (INV-904) */
            r->pos = q + P;
            r->last_end = r->pos;
            r->last = LAST_OTHER;
        }
        if (r->pos > r->len)
            r->pos = r->len;
    }
}

static void append(feld_recogniser *r, const int *column, int blank)
{
    int i;
    if (r->len + r->P > r->cap)
        compact(r);
    if (r->len + r->P > r->cap) {
        /* cannot happen while scan keeps up; drop the oldest data rather than overflow
         * (whole columns, as compact) */
        int d = r->len + r->P - r->cap;
        d += (r->P - d % r->P) % r->P;
        memmove(r->s, r->s + d, (size_t)(r->len - d) * sizeof(double));
        r->len -= d;
        r->pos = r->pos > d ? r->pos - d : 0;
        r->last_end -= d;
    }
    for (i = 0; i < r->P; i++) {
        double v = blank ? (r->ink_high ? 0.0 : 255.0) : (double)column[i];
        r->s[r->len + i] = r->ink_high ? v / 255.0 : 1.0 - v / 255.0;
    }
    r->len += r->P;
}

void feld_recogniser_column(feld_recogniser *r, const int *column, ffeldhell_char *out,
                            size_t max, size_t *n, size_t *dropped)
{
    append(r, column, 0);
    scan(r, 0, out, max, n, dropped);
}

void feld_recogniser_flush(feld_recogniser *r, ffeldhell_char *out, size_t max, size_t *n,
                           size_t *dropped)
{
    int i;
    for (i = 0; i < r->wmax + 4; i++)
        append(r, NULL, 1);
    scan(r, 1, out, max, n, dropped);
    feld_recogniser_reset(r);
}

size_t ffeldhell_recognise(const unsigned char *columns, size_t n_columns, int height,
                           unsigned font_mask, double floor, ffeldhell_char *out, size_t max)
{
    /* the recogniser on its own (allocates a temporary instance) */
    feld_recogniser *r;
    int col[FELD_MAX_RX_COLUMN_LEN];
    size_t k, n = 0, dropped = 0;
    int i;
    if (height < FFELDHELL_MIN_HEIGHT || height > FFELDHELL_MAX_HEIGHT || (!columns && n_columns))
        return 0;
    r = feld_recogniser_create(height, font_mask ? font_mask : FELD_DEFAULT_FONT_MASK,
                               floor > 0.0 ? floor : FELD_DEFAULT_FLOOR, 0);
    if (!r)
        return 0;
    for (k = 0; k < n_columns; k++) {
        for (i = 0; i < height; i++)
            col[i] = columns[k * (size_t)height + (size_t)i];
        feld_recogniser_column(r, col, out, out ? max : 0, &n, &dropped);
    }
    feld_recogniser_flush(r, out, out ? max : 0, &n, &dropped);
    feld_recogniser_destroy(r);
    return n;
}
