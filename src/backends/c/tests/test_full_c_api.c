#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "codec_g723_1.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void test_info_api(void) {
    printf("Testing basic info API...\n");
    uint32_t abi = g723_abi_version();
    assert(abi == G723_ABI_VERSION);

    const char *name = g723_backend_name();
    printf("  Backend name: %s\n", name);
    assert(strcmp(name, "c") == 0);

    assert(g723_backend_supports_bitrate(5300) == 1);
    assert(g723_backend_supports_bitrate(6300) == 1);
    assert(g723_backend_supports_bitrate(8000) == 0);
}

static void test_roundtrip_8k(int32_t bitrate, const char *rate_name) {
    printf("Testing %s 8000 Hz roundtrip...\n", rate_name);
    void *encoder = NULL;
    int32_t res = g723_encoder_create(bitrate, &encoder);
    assert(res == G723_RESULT_OK && encoder != NULL);

    void *decoder = NULL;
    res = g723_decoder_create(&decoder);
    assert(res == G723_RESULT_OK && decoder != NULL);

    // 1000 Hz sine at 8000 Hz
    int16_t pcm_in[G723_PCM_SAMPLES_PER_FRAME];
    for (size_t i = 0; i < G723_PCM_SAMPLES_PER_FRAME; i++) {
        pcm_in[i] = (int16_t)(10000.0 * sin(2.0 * M_PI * 1000.0 * (double)i / 8000.0));
    }

    uint8_t packet[32];
    uint32_t out_size = 0;
    res = g723_encode_frame(encoder, pcm_in, G723_PCM_SAMPLES_PER_FRAME, packet, sizeof(packet), &out_size);
    assert(res == G723_RESULT_OK);
    size_t expected = (bitrate == 6300) ? G723_FRAME_BYTES_6300 : G723_FRAME_BYTES_5300;
    assert(out_size == expected);

    int16_t pcm_out[G723_PCM_SAMPLES_PER_FRAME];
    uint32_t decoded_samples = 0;
    res = g723_decode_frame(decoder, packet, out_size, pcm_out, G723_PCM_SAMPLES_PER_FRAME, &decoded_samples);
    assert(res == G723_RESULT_OK);
    assert(decoded_samples == G723_PCM_SAMPLES_PER_FRAME);

    g723_encoder_destroy(encoder);
    g723_decoder_destroy(decoder);
}

static void test_roundtrip_16k(int32_t bitrate, const char *rate_name) {
    printf("Testing %s 16000 Hz roundtrip...\n", rate_name);
    void *encoder = NULL;
    int32_t res = g723_encoder_create(bitrate, &encoder);
    assert(res == G723_RESULT_OK && encoder != NULL);

    void *decoder = NULL;
    res = g723_decoder_create(&decoder);
    assert(res == G723_RESULT_OK && decoder != NULL);

    res = g723_decoder_set_sample_rate(decoder, G723_PCM_SAMPLE_RATE_16K);
    assert(res == G723_RESULT_OK);

    // 1000 Hz sine at 16000 Hz
    int16_t pcm_in[G723_PCM_SAMPLES_PER_FRAME_16K];
    for (size_t i = 0; i < G723_PCM_SAMPLES_PER_FRAME_16K; i++) {
        pcm_in[i] = (int16_t)(10000.0 * sin(2.0 * M_PI * 1000.0 * (double)i / 16000.0));
    }

    uint8_t packet[32];
    uint32_t out_size = 0;
    res = g723_encode_frame(encoder, pcm_in, G723_PCM_SAMPLES_PER_FRAME_16K, packet, sizeof(packet), &out_size);
    assert(res == G723_RESULT_OK);
    size_t expected = (bitrate == 6300) ? G723_FRAME_BYTES_6300 : G723_FRAME_BYTES_5300;
    assert(out_size == expected);

    int16_t pcm_out[G723_PCM_SAMPLES_PER_FRAME_16K];
    uint32_t decoded_samples = 0;
    res = g723_decode_frame(decoder, packet, out_size, pcm_out, G723_PCM_SAMPLES_PER_FRAME_16K, &decoded_samples);
    assert(res == G723_RESULT_OK);
    assert(decoded_samples == G723_PCM_SAMPLES_PER_FRAME_16K);

    g723_encoder_destroy(encoder);
    g723_decoder_destroy(decoder);
}

int main(void) {
    printf("=== Starting G.723.1 Full C API Tests ===\n");
    test_info_api();
    test_roundtrip_8k(6300, "6.3 kbps");
    test_roundtrip_8k(5300, "5.3 kbps");
    test_roundtrip_16k(6300, "6.3 kbps");
    test_roundtrip_16k(5300, "5.3 kbps");
    printf("=== All G.723.1 Full C API Tests Passed! ===\n");
    return 0;
}
