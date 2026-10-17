/*
 * fldigi-feldhell-core CPU and memory benchmark: receiver (raster and recogniser) and
 * transmitter speed per submode in multiples of real time on one core at 8000 Hz, the CPU
 * share of one receiver, and the receiver's allocation per instance (wave-owl R-901.5:
 * < 5 % of one core for every submode, the fastest Hell X9 included, and < 2 MB; Slow
 * Hell's AGC average alone holds 91428 samples, quirk Q7).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include "test_util.h"

#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(void)
{
    int i, fail = 0;
    printf("%-9s %10s %8s %10s %8s\n", "submode", "RX x real", "RX CPU", "TX x real", "RX KiB");
    for (i = 0; i < ffeldhell_submode_count(); i++) {
        const ffeldhell_submode *m = ffeldhell_submode_at(i);
        size_t n, k, len = (size_t)FFELDHELL_RATE * 60;
        double *x, *y, t0, t_tx, t_rx, rx_speed, share;
        ffeldhell_rx *rx;
        rx_result *r = (rx_result *)calloc(1, sizeof *r);
        t0 = now();
        x = render(m->name, "CQ CQ DE DG6FL DG6FL K", 4, &n);
        t_tx = now() - t0;
        y = (double *)calloc(len, sizeof(double));
        for (k = 0; k < len; k++)
            y[k] = 0.25 * x[k % n];
        rx = make_rx(m->name);
        t0 = now();
        receive(rx, y, len, 512, r);
        t_rx = now() - t0;
        rx_speed = (double)len / FFELDHELL_RATE / t_rx;
        share = 100.0 / rx_speed;
        printf("%-9s %10.0f %7.2f%% %10.0f %8.0f\n", m->name, rx_speed, share,
               (double)n / FFELDHELL_RATE / t_tx, ffeldhell_rx_footprint(rx) / 1024.0);
        fail |= share >= 5.0 || ffeldhell_rx_footprint(rx) >= 2 * 1024 * 1024;
        free(r->raster);
        free(r);
        free(x);
        free(y);
        ffeldhell_rx_destroy(rx);
    }
    if (fail)
        fprintf(stderr, "budget exceeded (R-901.5: < 5 %% CPU, < 2 MB per receiver)\n");
    return fail;
}
