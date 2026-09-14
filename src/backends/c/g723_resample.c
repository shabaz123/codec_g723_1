#include "g723_resample.h"
#include "g723_basicop.h"
#include <string.h>

/*
 * Linear-phase FIR lowpass filter for 2:1 decimation (16k -> 8k).
 * Cutoff ~ 3.6 kHz (fc = 0.225 Fs), stopband > 60 dB.
 * Sum = 32768 in Q15 (unity DC gain).
 */
static const int16_t G723_DOWN_FIR_Q15[G723_DOWN_FILTER_TAPS] = {
    0, 2, -6, -33, 11, 136, 36, -359,
    -267, 717, 929, -1144, -2631, 1499, 10118, 14752,
    10118, 1499, -2631, -1144, 929, 717, -267, -359,
    36, 136, 11, -33, -6, 2, 0
};

/*
 * Polyphase components for 1:2 interpolation (8k -> 16k).
 * Phase 0: even phase (16 taps), sum = 32768 in Q15.
 * Phase 1: odd phase (15 taps), sum = 32768 in Q15.
 */
static const int16_t G723_UP_P0_Q15[G723_UP_FILTER_TAPS_P0] = {
    0, -12, 23, 73, -533, 1859, -5262, 20237,
    20235, -5262, 1859, -533, 73, 23, -12, 0
};

static const int16_t G723_UP_P1_Q15[G723_UP_FILTER_TAPS_P1] = {
    5, -66, 271, -718, 1434, -2288, 2999, 29494,
    2999, -2288, 1434, -718, 271, -66, 5
};

void g723_resampler_init(g723_resampler_state_t *state) {
    if (state == NULL) return;
    memset(state, 0, sizeof(g723_resampler_state_t));
}

void g723_resampler_reset(g723_resampler_state_t *state) {
    g723_resampler_init(state);
}

void g723_resample_16k_to_8k(
    g723_resampler_state_t *state,
    const int16_t in_16k[G723_16K_SAMPLES_PER_FRAME],
    int16_t out_8k[G723_8K_SAMPLES_PER_FRAME]
) {
    if (in_16k == NULL || out_8k == NULL) return;

    /* Extended input buffer: [history (30) | input (480)] */
    int16_t ext[G723_DOWN_HIST_LEN + G723_16K_SAMPLES_PER_FRAME];
    if (state != NULL) {
        memcpy(&ext[0], state->down_hist, sizeof(int16_t) * G723_DOWN_HIST_LEN);
    } else {
        memset(&ext[0], 0, sizeof(int16_t) * G723_DOWN_HIST_LEN);
    }
    memcpy(&ext[G723_DOWN_HIST_LEN], in_16k, sizeof(int16_t) * G723_16K_SAMPLES_PER_FRAME);

    if (state != NULL) {
        memcpy(state->down_hist,
               &in_16k[G723_16K_SAMPLES_PER_FRAME - G723_DOWN_HIST_LEN],
               sizeof(int16_t) * G723_DOWN_HIST_LEN);
    }

    for (size_t m = 0; m < G723_8K_SAMPLES_PER_FRAME; m++) {
        int64_t acc = 1 << 14; /* Rounding */
        size_t base = 2 * m;
        for (size_t k = 0; k < G723_DOWN_FILTER_TAPS; k++) {
            acc += (int64_t)G723_DOWN_FIR_Q15[k] * (int64_t)ext[base + (G723_DOWN_FILTER_TAPS - 1 - k)];
        }
        out_8k[m] = g723_saturate16((int32_t)(acc >> 15));
    }
}

void g723_resample_8k_to_16k(
    g723_resampler_state_t *state,
    const int16_t in_8k[G723_8K_SAMPLES_PER_FRAME],
    int16_t out_16k[G723_16K_SAMPLES_PER_FRAME]
) {
    if (in_8k == NULL || out_16k == NULL) return;

    /* Extended input buffer: [history (16) | input (240)] */
    int16_t ext[G723_UP_HIST_LEN + G723_8K_SAMPLES_PER_FRAME];
    if (state != NULL) {
        memcpy(&ext[0], state->up_hist, sizeof(int16_t) * G723_UP_HIST_LEN);
    } else {
        memset(&ext[0], 0, sizeof(int16_t) * G723_UP_HIST_LEN);
    }
    memcpy(&ext[G723_UP_HIST_LEN], in_8k, sizeof(int16_t) * G723_8K_SAMPLES_PER_FRAME);

    if (state != NULL) {
        memcpy(state->up_hist,
               &in_8k[G723_8K_SAMPLES_PER_FRAME - G723_UP_HIST_LEN],
               sizeof(int16_t) * G723_UP_HIST_LEN);
    }

    for (size_t m = 0; m < G723_8K_SAMPLES_PER_FRAME; m++) {
        /* Phase 0 (even output sample) */
        int64_t acc0 = 1 << 14;
        for (size_t k = 0; k < G723_UP_FILTER_TAPS_P0; k++) {
            acc0 += (int64_t)G723_UP_P0_Q15[k] * (int64_t)ext[m + (G723_UP_FILTER_TAPS_P0 - 1 - k)];
        }
        out_16k[2 * m] = g723_saturate16((int32_t)(acc0 >> 15));

        /* Phase 1 (odd output sample) */
        int64_t acc1 = 1 << 14;
        for (size_t k = 0; k < G723_UP_FILTER_TAPS_P1; k++) {
            acc1 += (int64_t)G723_UP_P1_Q15[k] * (int64_t)ext[m + 1 + (G723_UP_FILTER_TAPS_P1 - 1 - k)];
        }
        out_16k[2 * m + 1] = g723_saturate16((int32_t)(acc1 >> 15));
    }
}
