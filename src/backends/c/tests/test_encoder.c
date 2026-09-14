#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "g723_encoder.h"
#include "g723_qdec.h"
#include "g723_consts.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void generate_test_signal(size_t n_samples, int16_t *out) {
    // Generate a speech-like multi-tone harmonic signal (e.g., pitch ~ 160 Hz with harmonics)
    for (size_t i = 0; i < n_samples; i++) {
        double t = (double)i / 8000.0;
        double s = 0.5 * sin(2.0 * M_PI * 160.0 * t)
                 + 0.3 * sin(2.0 * M_PI * 320.0 * t)
                 + 0.2 * sin(2.0 * M_PI * 480.0 * t)
                 + 0.1 * sin(2.0 * M_PI * 960.0 * t);
        s *= 10000.0;
        if (s > 32767.0) s = 32767.0;
        if (s < -32768.0) s = -32768.0;
        out[i] = (int16_t)s;
    }
}

static void test_encode_decode_roundtrip(g723_rate_t rate, const char *rate_name) {
    printf("Testing %s encode/decode roundtrip across 10 frames...\n", rate_name);
    g723_encoder_state_t enc;
    g723_encoder_init(&enc, rate);

    g723_qdec_state_t dec;
    g723_qdec_init(&dec);
    g723_qdec_set_postfilter(&dec, false); // For SNR measurement without postfilter spectral coloration

    const size_t NUM_FRAMES = 10;
    const size_t TOTAL_SAMPLES = NUM_FRAMES * G723_FRAME_SIZE_SAMPLES;
    int16_t orig_pcm[TOTAL_SAMPLES];
    generate_test_signal(TOTAL_SAMPLES, orig_pcm);

    int16_t dec_pcm[TOTAL_SAMPLES];
    size_t expected_bytes = (rate == G723_RATE_HIGH) ? G723_HIGH_RATE_BYTES : G723_LOW_RATE_BYTES;

    for (size_t f = 0; f < NUM_FRAMES; f++) {
        const int16_t *in_frame = &orig_pcm[f * G723_FRAME_SIZE_SAMPLES];
        uint8_t packet[32];
        size_t packet_size = sizeof(packet);

        g723_result_t enc_res = g723_encoder_encode_frame(&enc, in_frame, packet, &packet_size);
        assert(enc_res == G723_RESULT_OK);
        assert(packet_size == expected_bytes);

        // First byte rate discriminator check
        assert((packet[0] & 3) == (rate == G723_RATE_HIGH ? 0 : 1));

        int16_t *out_frame = &dec_pcm[f * G723_FRAME_SIZE_SAMPLES];
        g723_result_t dec_res = g723_qdec_decode_frame(&dec, packet, packet_size, out_frame);
        assert(dec_res == G723_RESULT_OK);
    }

    // Measure signal power in decoded output (skipping cold start lookahead delay)
    // Note: the encoder has 60 samples lookahead (delay = 60 samples)
    int64_t in_pwr = 0, out_pwr = 0;
    for (size_t i = 60; i < TOTAL_SAMPLES - 60; i++) {
        in_pwr += (int64_t)orig_pcm[i] * orig_pcm[i];
        out_pwr += (int64_t)dec_pcm[i + 60] * dec_pcm[i + 60];
    }
    printf("  %s: Input energy = %lld, Decoded energy = %lld\n",
           rate_name, (long long)in_pwr, (long long)out_pwr);
    assert(out_pwr > in_pwr / 10); // Energy successfully transmitted and reconstructed

    // Check correlation between input (delayed by 60 samples) and output
    double cross = 0.0, e_in = 0.0, e_out = 0.0;
    for (size_t i = 60; i < TOTAL_SAMPLES - 60; i++) {
        double xi = (double)orig_pcm[i];
        double yi = (double)dec_pcm[i + 60];
        cross += xi * yi;
        e_in += xi * xi;
        e_out += yi * yi;
    }
    double corr = cross / sqrt(e_in * e_out);
    printf("  %s: Waveform correlation = %.4f\n", rate_name, corr);
    assert(corr > 0.65); // Clear positive correlation with the harmonic source
}

static void test_silence_roundtrip(void) {
    printf("Testing silence encoding and decoding...\n");
    g723_encoder_state_t enc;
    g723_encoder_init(&enc, G723_RATE_HIGH);

    g723_qdec_state_t dec;
    g723_qdec_init(&dec);

    int16_t zero_pcm[G723_FRAME_SIZE_SAMPLES] = {0};
    uint8_t packet[32];
    size_t packet_size = sizeof(packet);

    g723_result_t res = g723_encoder_encode_frame(&enc, zero_pcm, packet, &packet_size);
    assert(res == G723_RESULT_OK);
    assert(packet_size == G723_HIGH_RATE_BYTES);

    int16_t out_pcm[G723_FRAME_SIZE_SAMPLES];
    res = g723_qdec_decode_frame(&dec, packet, packet_size, out_pcm);
    assert(res == G723_RESULT_OK);

    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) {
        assert(abs(out_pcm[i]) < 100);
    }
}

int main(void) {
    printf("=== Starting G.723.1 Encoder Tests ===\n");
    test_encode_decode_roundtrip(G723_RATE_HIGH, "6.3 kbps MP-MLQ");
    test_encode_decode_roundtrip(G723_RATE_LOW,  "5.3 kbps ACELP");
    test_silence_roundtrip();
    printf("=== All G.723.1 Encoder Tests Passed! ===\n");
    return 0;
}
