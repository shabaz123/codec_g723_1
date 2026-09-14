#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "g723_resample.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void test_resample_sine_roundtrip(void) {
    printf("Testing 16k -> 8k -> 16k sine wave roundtrip...\n");
    g723_resampler_state_t down_state, up_state;
    g723_resampler_init(&down_state);
    g723_resampler_init(&up_state);

    const size_t NUM_FRAMES = 4;
    int16_t orig_16k[NUM_FRAMES * G723_16K_SAMPLES_PER_FRAME];
    int16_t down_8k[NUM_FRAMES * G723_8K_SAMPLES_PER_FRAME];
    int16_t up_16k[NUM_FRAMES * G723_16K_SAMPLES_PER_FRAME];

    // 1000 Hz sine wave at 16000 Hz
    for (size_t i = 0; i < NUM_FRAMES * G723_16K_SAMPLES_PER_FRAME; i++) {
        double t = (double)i / 16000.0;
        double s = 10000.0 * sin(2.0 * M_PI * 1000.0 * t);
        orig_16k[i] = (int16_t)s;
    }

    for (size_t f = 0; f < NUM_FRAMES; f++) {
        g723_resample_16k_to_8k(&down_state,
                                &orig_16k[f * G723_16K_SAMPLES_PER_FRAME],
                                &down_8k[f * G723_8K_SAMPLES_PER_FRAME]);

        g723_resample_8k_to_16k(&up_state,
                                &down_8k[f * G723_8K_SAMPLES_PER_FRAME],
                                &up_16k[f * G723_16K_SAMPLES_PER_FRAME]);
    }

    // Check correlation after filter group delay (48 samples at 16k)
    size_t delay = 48;
    double cross = 0.0, e_orig = 0.0, e_up = 0.0;
    for (size_t i = 0; i < (NUM_FRAMES - 1) * G723_16K_SAMPLES_PER_FRAME - delay; i++) {
        double xo = (double)orig_16k[i];
        double xu = (double)up_16k[i + delay];
        cross += xo * xu;
        e_orig += xo * xo;
        e_up += xu * xu;
    }
    double corr = cross / sqrt(e_orig * e_up);
    printf("  Waveform correlation = %.6f\n", corr);
    assert(corr > 0.99); // Excellent phase linearity and spectral preservation
}

static void test_dc_preservation(void) {
    printf("Testing DC preservation...\n");
    g723_resampler_state_t down_state, up_state;
    g723_resampler_init(&down_state);
    g723_resampler_init(&up_state);

    int16_t const_16k[G723_16K_SAMPLES_PER_FRAME];
    for (size_t i = 0; i < G723_16K_SAMPLES_PER_FRAME; i++) const_16k[i] = 10000;

    int16_t down_8k[G723_8K_SAMPLES_PER_FRAME];
    int16_t up_16k[G723_16K_SAMPLES_PER_FRAME];

    // Warm up filter history
    g723_resample_16k_to_8k(&down_state, const_16k, down_8k);
    g723_resample_8k_to_16k(&up_state, down_8k, up_16k);

    // Second frame should have exact DC output
    g723_resample_16k_to_8k(&down_state, const_16k, down_8k);
    g723_resample_8k_to_16k(&up_state, down_8k, up_16k);

    for (size_t i = 50; i < G723_8K_SAMPLES_PER_FRAME; i++) {
        assert(abs(down_8k[i] - 10000) <= 2);
    }
    for (size_t i = 50; i < G723_16K_SAMPLES_PER_FRAME; i++) {
        assert(abs(up_16k[i] - 10000) <= 2);
    }
}

int main(void) {
    printf("=== Starting G.723.1 Resampler Tests ===\n");
    test_resample_sine_roundtrip();
    test_dc_preservation();
    printf("=== All G.723.1 Resampler Tests Passed! ===\n");
    return 0;
}
