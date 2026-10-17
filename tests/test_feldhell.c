/*
 * fldigi-feldhell-core unit tests: API, submode table, fonts, transmitter framing, receiver
 * loopback for every submode, reverse, chunk invariance, polarity, squelch, the recogniser's
 * "no match" rule and the memory bound.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "test_util.h"

static const char *CQ = "CQ CQ DE DG6FL DG6FL K";

static void test_version(void)
{
    const char *v = ffeldhell_version();
    CHECK(strstr(v, "contract=feldhell-c1") != NULL, "contract tag: %s", v);
    CHECK(strstr(v, "abi=1") != NULL && strstr(v, "tx=1") != NULL, "abi/tx tags: %s", v);
    CHECK(strstr(v, "dsp=0.5.0") != NULL, "dsp tag: %s", v);
    CHECK(ffeldhell_abi() == FFELDHELL_ABI, "abi");
}

static void test_submodes(void)
{
    static const char *names[7] = {"FELDHELL", "SLOWHELL", "HELLX5", "HELLX9",
                                   "FSKH245", "FSKH105", "HELL80"};
    int i;
    CHECK(ffeldhell_submode_count() == 7, "7 submodes");
    CHECK(ffeldhell_submode_at(7) == NULL && ffeldhell_submode_at(-1) == NULL, "bounds");
    CHECK(ffeldhell_submode_find("feldhell") == ffeldhell_submode_at(0), "case-insensitive");
    CHECK(ffeldhell_submode_find("HELL") == NULL, "unknown");
    for (i = 0; i < 7; i++) {
        const ffeldhell_submode *m = ffeldhell_submode_at(i);
        double k = m->fm ? 4.0 : 1.2, bw = m->fm ? m->hell_bandwidth : 14 * m->column_rate;
        CHECK(strcmp(m->name, names[i]) == 0, "name %d", i);
        CHECK(m->fldigi_id == 31 + i, "fldigi id %s", m->name);
        CHECK(m->fm == (i >= 4), "family %s", m->name);
        /* quirk Q6: restart's filter_bandwidth, AM 1.2 x and FM 4.0 x the bandwidth */
        CHECK(m->filter_bandwidth == 5 * round(k * bw / 5.0), "filter bw %s", m->name);
        CHECK(m->hell_bandwidth == bw, "bandwidth %s", m->name);
        CHECK(m->baud == 0.5 * 14 * m->column_rate, "baud %s", m->name);
    }
    CHECK(ffeldhell_submode_at(0)->filter_bandwidth == 295.0, "Feld Hell 295 Hz");
    CHECK(ffeldhell_submode_at(4)->filter_bandwidth == 490.0, "FSKH245 490 Hz");
}

static void test_fonts(void)
{
    int cols[32], n, i, row;
    CHECK(ffeldhell_font_count() == 15, "15 fonts");
    CHECK(strcmp(ffeldhell_font_name(0), "7x7") == 0, "font 0");
    CHECK(strcmp(ffeldhell_font_name(4), "hell") == 0, "font 4");
    CHECK(ffeldhell_font_name(15) == NULL, "font bounds");
    CHECK(ffeldhell_font_find("WIDE") == 14 && ffeldhell_font_find("x") == -1, "font find");
    CHECK(ffeldhell_font_columns(0, ' ', cols, 32) == 0, "space has no columns");
    CHECK(ffeldhell_font_columns(0, 0x7F, cols, 32) == 0, "DEL has no columns");
    CHECK(ffeldhell_font_columns(15, 'A', cols, 32) == -1, "unknown font");
    /* 'A' of feld7x7_14 (Feld7x7-14.cxx): rows 2-3 0x7000, 4-5 0x8800, 6-7 0xF800, 8-11
     * 0x8800; get_font_data sends row 13 - r as bit r (feld.cxx:552-556), trims at 5 */
    n = ffeldhell_font_columns(0, 'A', cols, 32);
    CHECK(n == 5, "A is 5 columns wide (%d)", n);
    for (i = 0; i < n && i < 5; i++) {
        static const int rows[14] = {0, 0, 0x7000, 0x7000, 0x8800, 0x8800, 0xF800, 0xF800,
                                     0x8800, 0x8800, 0x8800, 0x8800, 0, 0};
        int want = 0;
        for (row = 0; row < 14; row++)
            if (rows[13 - row] & (1 << (15 - i)))
                want |= 1 << row;
        CHECK(cols[i] == want, "A column %d: %x vs %x", i, cols[i], want);
    }
    for (i = 0; i < 15; i++) {
        int c, any = 0;
        for (c = '!'; c <= '~'; c++)
            any += ffeldhell_font_columns(i, (unsigned char)c, cols, 32) > 0;
        CHECK(any >= 90, "font %d has glyphs (%d)", i, any);
    }
}

static void test_create(void)
{
    ffeldhell_rx_config r;
    ffeldhell_tx_config t;
    ffeldhell_rx *rx;
    ffeldhell_tx *tx;
    ffeldhell_rx_config_default(&r, "HELLX9");
    r.carrier_hz = 1000.0;      /* X9 is 2205 Hz wide: 1102.5 .. 1897.5 */
    CHECK(ffeldhell_rx_create(&r) == NULL, "carrier out of range");
    r.carrier_hz = 1500.0;
    rx = ffeldhell_rx_create(&r);
    CHECK(rx != NULL, "X9 at 1500 Hz");
    CHECK(ffeldhell_rx_set_carrier(rx, 1000.0) == -1 && ffeldhell_rx_carrier_hz(rx) == 1500.0,
          "retune refused");
    ffeldhell_rx_destroy(rx);
    r.submode = "NOPE";
    CHECK(ffeldhell_rx_create(&r) == NULL, "unknown submode");
    ffeldhell_rx_config_default(&r, "FELDHELL");
    r.height = 13;
    CHECK(ffeldhell_rx_create(&r) == NULL, "height 13");
    r.height = 43;
    CHECK(ffeldhell_rx_create(&r) == NULL, "height 43");
    r.height = 20;
    r.agc = 4;
    CHECK(ffeldhell_rx_create(&r) == NULL, "agc 4");
    r.agc = 2;
    r.font_mask = 1u << 15;
    CHECK(ffeldhell_rx_create(&r) == NULL, "font mask");
    ffeldhell_tx_config_default(&t, "FELDHELL");
    CHECK(t.font == 4 && t.pulse == 1 && t.width == 1 && !t.idle_dots, "fldigi TX defaults");
    t.font = 15;
    CHECK(ffeldhell_tx_create(&t) == NULL, "font 15");
    t.font = 4;
    t.width = 4;
    CHECK(ffeldhell_tx_create(&t) == NULL, "width 4");
    t.width = 1;
    t.pulse = 4;
    CHECK(ffeldhell_tx_create(&t) == NULL, "pulse 4");
    t.pulse = 1;
    tx = ffeldhell_tx_create(&t);
    CHECK(tx != NULL, "tx");
    CHECK(ffeldhell_tx_put(tx, 'A', NULL, 0) == 0, "no buffer");
    ffeldhell_tx_destroy(tx);
}

static void test_tx_framing(void)
{
    /* quirks Q8 (blank column around every character, 3 more for a space) and Q9 (each
     * glyph column HellXmtWidth times); the sample count follows the column count */
    ffeldhell_tx_config c;
    ffeldhell_tx *tx;
    double *buf, spc;
    size_t bound, n;
    int w;
    ffeldhell_tx_config_default(&c, "FELDHELL");
    tx = ffeldhell_tx_create(&c);
    bound = ffeldhell_tx_bound(tx);
    buf = (double *)malloc(bound * sizeof(double));
    spc = ffeldhell_tx_samples_per_column(tx);
    CHECK(ffeldhell_tx_columns(tx, ' ') == 5 && ffeldhell_tx_columns(tx, '\n') == 5, "space");
    w = ffeldhell_font_columns(4, 'H', NULL, 0);
    CHECK(ffeldhell_tx_columns(tx, 'H') == 2 + w, "H = 2 + %d", w);
    CHECK(ffeldhell_tx_columns(tx, '\t') == 2, "control character: brackets only");
    ffeldhell_tx_begin(tx, buf, bound);
    n = ffeldhell_tx_put(tx, ' ', buf, bound);
    CHECK(fabs(n - 5 * spc) <= 14, "space samples %zu vs %.1f", n, 5 * spc);
    n = ffeldhell_tx_put(tx, '\r', buf, bound);
    CHECK(fabs(n - 5 * spc) <= 14, "CR is a space");
    n = ffeldhell_tx_idle(tx, buf, bound);
    CHECK(fabs(n - 2 * spc) <= 14, "idle: 2 blank columns (%zu)", n);
    for (n = 0; n < bound; n++)
        buf[n] = 1.0;
    n = ffeldhell_tx_idle(tx, buf, bound);
    {
        size_t i, nz = 0;
        for (i = 0; i < n; i++)
            nz += buf[i] != 0.0;
        CHECK(nz == 0, "idle columns are silent (AM)");
    }
    ffeldhell_tx_destroy(tx);
    c.width = 2;
    tx = ffeldhell_tx_create(&c);
    CHECK(ffeldhell_tx_columns(tx, 'H') == 2 + 2 * w, "width 2 doubles the glyph");
    ffeldhell_tx_destroy(tx);
    c.width = 1;
    c.idle_dots = 1;
    tx = ffeldhell_tx_create(&c);
    ffeldhell_tx_begin(tx, buf, bound);
    n = ffeldhell_tx_idle(tx, buf, bound);
    CHECK(fabs(n - ffeldhell_tx_columns(tx, '.') * spc) <= 14, "idle dot");
    n = ffeldhell_tx_end(tx, buf, bound);
    CHECK(fabs(n - (3 * ffeldhell_tx_columns(tx, '.') + 5) * spc) <= 14, "postamble");
    CHECK(n <= bound, "bound");
    ffeldhell_tx_destroy(tx);
    free(buf);
}

static void loopback(const char *sub, int font, int reverse_tx, int reverse_rx, int expect)
{
    ffeldhell_tx_config c;
    ffeldhell_rx_config rc;
    ffeldhell_rx *rx;
    rx_result *r = (rx_result *)calloc(1, sizeof *r);
    double *x, *y;
    size_t n, ny, i, d;
    ffeldhell_tx_config_default(&c, sub);
    c.font = font;
    c.reverse = reverse_tx;
    x = render_cfg(&c, CQ, 0, &n);
    y = pad_level(x, n, 1.0, 1.0, &ny);
    ffeldhell_rx_config_default(&rc, sub);
    rc.reverse = reverse_rx;
    rx = ffeldhell_rx_create(&rc);
    receive(rx, y, ny, 512, r);
    d = window_distance(CQ, r->text);
    if (expect) {
        /* AC-900.1: the sent text, every character at or above the floor */
        const char *p = strstr(r->text, "CQ");
        CHECK(d == 0, "%s font %d rev %d/%d: '%s'", sub, font, reverse_tx, reverse_rx, r->text);
        for (i = 0; p && i < strlen(CQ); i++)
            CHECK(r->conf[(size_t)(p - r->text) + i] >= 0.55, "%s confidence", sub);
    } else {
        CHECK(d > strlen(CQ) / 2, "%s mirrored shift must not decode: '%s'", sub, r->text);
    }
    free(r->raster);
    free(r);
    free(x);
    free(y);
    ffeldhell_rx_destroy(rx);
}

static void test_loopback(void)
{
    int i;
    for (i = 0; i < ffeldhell_submode_count(); i++) {
        const char *sub = ffeldhell_submode_at(i)->name;
        loopback(sub, 4, 0, 0, 1);
        loopback(sub, 0, 0, 0, 1);
    }
    /* quirk Q5: the FM reverse flag mirrors the shift on both sides */
    loopback("FSKH245", 4, 1, 1, 1);
    loopback("HELL80", 4, 1, 1, 1);
    loopback("FSKH245", 4, 1, 0, 0);
}

static void test_chunk_invariance(void)
{
    static const char *subs[3] = {"FELDHELL", "HELLX9", "FSKH105"};
    static const size_t chunks[4] = {1, 37, 512, 1u << 30};
    int s, k;
    for (s = 0; s < 3; s++) {
        double *x, *y;
        size_t n, ny;
        rx_result *ref = (rx_result *)calloc(1, sizeof *ref);
        x = render(subs[s], "HELL 73", 4, &n);
        y = pad_level(x, n, 0.5, 0.5, &ny);
        for (k = 0; k < 4; k++) {
            rx_result *r = (rx_result *)calloc(1, sizeof *r);
            ffeldhell_rx *rx = make_rx(subs[s]);
            receive(rx, y, ny, chunks[k], k ? r : ref);
            if (k) {
                CHECK(r->columns == ref->columns &&
                          memcmp(r->raster, ref->raster, r->columns * 20) == 0,
                      "%s raster chunk %zu", subs[s], chunks[k]);
                CHECK(strcmp(r->text, ref->text) == 0, "%s text chunk %zu", subs[s], chunks[k]);
                free(r->raster);
            }
            free(r);
            ffeldhell_rx_destroy(rx);
        }
        CHECK(strstr(ref->text, "HELL 73") != NULL, "%s: '%s'", subs[s], ref->text);
        free(ref->raster);
        free(ref);
        free(x);
        free(y);
    }
}

static void test_reset_polarity_squelch(void)
{
    double *x, *y;
    size_t n, ny, i, diff = 0;
    rx_result *a = (rx_result *)calloc(1, sizeof *a), *b = (rx_result *)calloc(1, sizeof *b);
    ffeldhell_rx_config c;
    ffeldhell_rx *rx;
    x = render("FELDHELL", "DE DG6FL", 4, &n);
    y = pad_level(x, n, 0.5, 0.5, &ny);
    rx = make_rx("FELDHELL");
    receive(rx, y, ny, 512, a);
    ffeldhell_rx_reset(rx);
    receive(rx, y, ny, 512, b);
    CHECK(a->columns == b->columns && memcmp(a->raster, b->raster, a->columns * 20) == 0 &&
              strcmp(a->text, b->text) == 0, "reset gives the same result");
    ffeldhell_rx_destroy(rx);
    free(b->raster);
    /* quirk Q4: blackboard draws the AM raster as the complement, text unchanged */
    ffeldhell_rx_config_default(&c, "FELDHELL");
    c.blackboard = 1;
    rx = ffeldhell_rx_create(&c);
    receive(rx, y, ny, 512, b);
    for (i = 0; i < a->columns * 20 && b->columns == a->columns; i++)
        diff += b->raster[i] != 255 - a->raster[i];
    CHECK(b->columns == a->columns && diff == 0, "blackboard complement (%zu)", diff);
    CHECK(strcmp(a->text, b->text) == 0, "blackboard text '%s' vs '%s'", b->text, a->text);
    ffeldhell_rx_destroy(rx);
    free(b->raster);
    /* quirk Q3: squelch above the metric's range hides every column */
    ffeldhell_rx_config_default(&c, "FELDHELL");
    c.squelch_on = 1;
    c.squelch = 100.0;
    rx = ffeldhell_rx_create(&c);
    receive(rx, y, ny, 512, b);
    CHECK(b->columns == 0 && b->n == 0, "squelched");
    CHECK(ffeldhell_rx_metric(rx) >= 0.0 && ffeldhell_rx_metric(rx) <= 100.0, "metric range");
    CHECK(ffeldhell_rx_footprint(rx) < 2 * 1024 * 1024, "footprint %zu", ffeldhell_rx_footprint(rx));
    ffeldhell_rx_destroy(rx);
    free(a->raster);
    free(b->raster);
    free(a);
    free(b);
    free(x);
    free(y);
}

static void test_no_guess(void)
{
    /* INV-904 / AC-900.4: noise rasters and glyphs of a font that is not selected give
     * "no match", never a character; nothing fails on any input */
    unsigned char *cols = (unsigned char *)malloc(4000 * 20);
    ffeldhell_char out[8192];
    size_t i, n, chars = 0;
    uint32_t s = 12345;
    for (i = 0; i < 4000 * 20; i++) {
        s = s * 1664525u + 1013904223u;
        cols[i] = (unsigned char)(s >> 24);
    }
    n = ffeldhell_recognise(cols, 4000, 20, 0, 0.0, out, 8192);
    for (i = 0; i < n; i++)
        chars += out[i].c != 0;
    CHECK(chars == 0, "noise produced %zu characters", chars);
    for (i = 0; i < n; i++)
        CHECK(out[i].confidence >= 0.0 && out[i].confidence <= 1.0, "confidence range");
    memset(cols, 0, 4000 * 20);
    n = ffeldhell_recognise(cols, 4000, 20, 0, 0.0, out, 8192);   /* all ink */
    for (i = 0, chars = 0; i < n; i++)
        chars += out[i].c != 0;
    CHECK(chars == 0, "solid ink produced %zu characters", chars);
    CHECK(ffeldhell_recognise(cols, 10, 13, 0, 0.0, out, 8192) == 0, "bad height");
    CHECK(ffeldhell_recognise(NULL, 0, 20, 0, 0.0, NULL, 0) == 0, "empty");
    free(cols);
}

int main(void)
{
    test_version();
    test_submodes();
    test_fonts();
    test_create();
    test_tx_framing();
    test_loopback();
    test_chunk_invariance();
    test_reset_polarity_squelch();
    test_no_guess();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("fldigi-feldhell-core: all tests passed (%s)\n", ffeldhell_version());
    return 0;
}
