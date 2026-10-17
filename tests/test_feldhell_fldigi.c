/*
 * fldigi-feldhell-core against fldigi 4.2.13 itself (goldens from
 * tests/golden/make_fldigi_golden.py, the headless fldigi bench build).
 *
 * TX, all 7 submodes (CQ and pangram overs), all 15 fonts, the pulse shapes 0-3 and the TX
 * widths 1-3: the port renders fldigi's TX vector (FNV-1a hash of the int16 output
 * identical on the reference platform; elsewhere the same length and, where fldigi's
 * vector is stored, at most 1 LSB from it).
 * RX, every submode clean and at -5 and -10 dB (2500 Hz), plus receive heights 14 and 42,
 * AGC 1 and 3, blackboard and the receive filter quirk Q10: the raster equals fldigi's
 * put_rx_data columns byte for byte on the reference platform; elsewhere the same number
 * of columns with the same recognised text (in noise: at most 1 + 5 % edits apart). Where the over is clean (and fldigi's own
 * filter passes it), the text recognised from fldigi's raster is the sent text (R-901.3).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include "test_util.h"

static const char *golden = "tests/golden";

static uint64_t fnv1a64(const int16_t *pcm, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ULL;
    size_t i;
    for (i = 0; i < n; i++) {
        uint16_t u = (uint16_t)pcm[i];
        h ^= (uint8_t)(u & 0xFF);
        h *= 0x100000001B3ULL;
        h ^= (uint8_t)(u >> 8);
        h *= 0x100000001B3ULL;
    }
    return h;
}

/* Same as noisy() in make_fldigi_golden.py; this file is compiled without FP contraction,
 * so every step rounds like Python. */
static uint32_t lcg;
static double lcg_uniform(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return lcg / 4294967296.0;
}

static double *noisy(const int16_t *pcm, size_t n, int sr, int snr_step, uint32_t seed,
                     size_t *out_n)
{
    size_t lead = (size_t)sr, tail = 2 * (size_t)sr, total = lead + n + tail, i;
    double *x = (double *)calloc(total, sizeof(double));
    double ps = 0, inv, s2, sd, gain;
    int k;
    for (i = 0; i < n; i++)
        x[lead + i] = (double)pcm[i];
    *out_n = total;
    if (snr_step == 0) {
        for (i = 0; i < total; i++)
            x[i] /= 32768.0;
        return x;
    }
    for (i = 0; i < n; i++)
        ps += (double)pcm[i] * (double)pcm[i];
    ps = ps / (double)n;
    inv = snr_step == 1 ? 1.0 / sqrt(10.0) : 1.0 / 10.0;
    s2 = ps / inv * (sr / 2.0) / 2500.0;
    sd = sqrt(s2);
    gain = 3276.7 / sqrt(ps * (double)n / (double)total + s2);
    lcg = seed;
    for (i = 0; i < total; i++) {
        double z = 0.0, y, q;
        for (k = 0; k < 12; k++)
            z += lcg_uniform();
        z -= 6.0;
        y = (x[i] + sd * z) * gain;
        q = floor(y + 0.5);
        if (q > 32767)
            q = 32767;
        if (q < -32768)
            q = -32768;
        x[i] = q / 32768.0;  /* libsndfile sf_readf_double: int16 / 32768 */
    }
    return x;
}

static char texts[2][256];

static const char *text_for(const char *key)
{
    return strcmp(key, "cq") == 0 ? texts[0] : texts[1];
}

static void load_texts(void)
{
    char path[512], *buf, *line, *save = NULL;
    size_t len;
    snprintf(path, sizeof path, "%s/texts.txt", golden);
    buf = (char *)read_file(path, &len);
    if (!buf) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strncmp(line, "cq ", 3) == 0)
            snprintf(texts[0], sizeof texts[0], "%s", line + 3);
        else if (strncmp(line, "pangram ", 8) == 0)
            snprintf(texts[1], sizeof texts[1], "%s", line + 8);
    }
    free(buf);
}

static int16_t *load_s16(const char *stem, size_t *n)
{
    char path[512];
    size_t len;
    int16_t *p;
    snprintf(path, sizeof path, "%s/tx/%s.s16", golden, stem);
    p = (int16_t *)read_file(path, &len);
    *n = p ? len / 2 : 0;
    return p;
}

static int tx_cases, rx_cases, raster_exact;

static void test_tx(void)
{
    char path[512], *buf, *line, *save = NULL;
    size_t len;
    snprintf(path, sizeof path, "%s/tx_hashes.txt", golden);
    buf = (char *)read_file(path, &len);
    CHECK(buf != NULL, "cannot read %s", path);
    if (!buf)
        return;
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char stem[64], sname[16], key[16], hash[32];
        int idx, sr, font, pulse, width;
        size_t samples, n, i, stored_n;
        unsigned long long want;
        ffeldhell_tx_config c;
        double *x;
        int16_t *pcm, *stored;
        if (line[0] == '#')
            continue;
        if (sscanf(line, "%63s %15s %15s %d %d %zu %d %d %d %31s", stem, sname, key, &idx, &sr,
                   &samples, &font, &pulse, &width, hash) != 10)
            continue;
        want = strtoull(hash, NULL, 16);
        ffeldhell_tx_config_default(&c, sname);
        c.font = font;
        c.pulse = pulse;
        c.width = width;
        x = render_cfg(&c, text_for(key), 0, &n);
        pcm = (int16_t *)malloc((n ? n : 1) * sizeof(int16_t));
        quantize(x, n, pcm);
        CHECK(n == samples, "TX %s: %zu samples, fldigi %zu", stem, n, samples);
        if (REFERENCE_PLATFORM)
            CHECK(fnv1a64(pcm, n) == want, "TX %s: hash differs from fldigi's", stem);
        stored = load_s16(stem, &stored_n);
        if (stored && stored_n == n) {
            int worst = 0;
            for (i = 0; i < n; i++) {
                int d = abs((int)pcm[i] - (int)stored[i]);
                if (d > worst)
                    worst = d;
            }
            CHECK(worst <= (REFERENCE_PLATFORM ? 0 : 1), "TX %s: %d LSB from fldigi", stem,
                  worst);
        }
        free(stored);
        free(pcm);
        free(x);
        tx_cases++;
    }
    free(buf);
}

static void recognise_text(const unsigned char *raster, size_t cols, int height, int bb,
                           char *out, size_t max)
{
    unsigned char *v = (unsigned char *)malloc(cols * (size_t)height + 1);
    ffeldhell_char *ch = (ffeldhell_char *)malloc(8192 * sizeof(ffeldhell_char));
    size_t i, n, k = 0;
    for (i = 0; i < cols * (size_t)height; i++)
        v[i] = bb ? (unsigned char)(255 - raster[i]) : raster[i];
    n = ffeldhell_recognise(v, cols, height, 0, 0.0, ch, 8192);
    for (i = 0; i < n && k + 1 < max; i++)
        if (ch[i].c)
            out[k++] = (char)ch[i].c;
    out[k] = 0;
    free(v);
    free(ch);
}

static void test_rx(void)
{
    static const char *snr_names[3] = {"clean", "m05", "m10"};
    char path[512], *buf, *line, *save = NULL;
    size_t len;
    snprintf(path, sizeof path, "%s/rx_cases.txt", golden);
    buf = (char *)read_file(path, &len);
    CHECK(buf != NULL, "cannot read %s", path);
    if (!buf)
        return;
    for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char stem[64], src[64], sname[16], snrs[64];
        int idx, height, agc, bb, step;
        double bw;
        size_t pcm_n;
        int16_t *pcm;
        if (line[0] == '#')
            continue;
        if (sscanf(line, "%63s %63s %15s %d %lf %d %d %d %63s", stem, src, sname, &idx, &bw,
                   &height, &agc, &bb, snrs) != 9)
            continue;
        pcm = load_s16(src, &pcm_n);
        CHECK(pcm != NULL, "RX %s: no source %s", stem, src);
        if (!pcm)
            continue;
        for (step = 0; step < 3; step++) {
            ffeldhell_rx_config c;
            ffeldhell_rx *rx;
            rx_result *r;
            unsigned char *ref;
            size_t ref_len, nx;
            double *x;
            char want_text[8192];
            if (!strstr(snrs, snr_names[step]))
                continue;
            snprintf(path, sizeof path, "%s/rx/%s_%s.raster", golden, stem, snr_names[step]);
            ref = (unsigned char *)read_file(path, &ref_len);
            CHECK(ref != NULL, "cannot read %s", path);
            if (!ref)
                continue;
            x = noisy(pcm, pcm_n, FFELDHELL_RATE, step, (uint32_t)(1000 * idx + step), &nx);
            ffeldhell_rx_config_default(&c, sname);
            c.rx_bw_hz = bw;
            c.height = height;
            c.agc = agc;
            c.blackboard = bb;
            rx = ffeldhell_rx_create(&c);
            r = (rx_result *)calloc(1, sizeof *r);
            receive(rx, x, nx, 512, r);
            CHECK(r->columns * (size_t)height == ref_len, "RX %s_%s: %zu columns, fldigi %zu",
                  stem, snr_names[step], r->columns, ref_len / (size_t)height);
            if (r->columns * (size_t)height == ref_len) {
                int same = memcmp(r->raster, ref, ref_len) == 0;
                raster_exact += same;
                if (REFERENCE_PLATFORM)
                    CHECK(same, "RX %s_%s: raster differs from fldigi's", stem, snr_names[step]);
            }
            recognise_text(ref, ref_len / (size_t)height, height, bb, want_text,
                           sizeof want_text);
            if (REFERENCE_PLATFORM || step == 0)
                CHECK(strcmp(r->text, want_text) == 0,
                      "RX %s_%s: text '%s', from fldigi's raster '%s'", stem, snr_names[step],
                      r->text, want_text);
            else    /* another libm: a pixel may round the other way in noise */
                CHECK(window_distance(want_text, r->text) <= 1 + strlen(want_text) / 20,
                      "RX %s_%s: text '%s', from fldigi's raster '%s'", stem, snr_names[step],
                      r->text, want_text);
            if (step == 0 && !strstr(stem, "_bw245"))
                CHECK(window_distance(texts[0], want_text) == 0,
                      "RX %s: fldigi's raster reads '%s'", stem, want_text);
            free(r->raster);
            free(r);
            free(x);
            free(ref);
            ffeldhell_rx_destroy(rx);
            rx_cases++;
        }
        free(pcm);
    }
    free(buf);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        golden = argv[1];
    load_texts();
    test_tx();
    test_rx();
    printf("fldigi goldens: %d TX overs, %d RX rasters (%d byte for byte)%s\n", tx_cases,
           rx_cases, raster_exact, REFERENCE_PLATFORM ? " [reference platform]" : "");
    CHECK(tx_cases >= 37 && rx_cases >= 32, "golden set incomplete");
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("fldigi-feldhell-core: fldigi goldens passed\n");
    return 0;
}
