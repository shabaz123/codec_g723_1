#ifndef CODEC_G723_1_H
#define CODEC_G723_1_H

#include <stdint.h>

#if defined(_WIN32)
#define G723_EXPORT __declspec(dllexport)
#else
#define G723_EXPORT \
    __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define G723_ABI_VERSION 1u

#define G723_PCM_SAMPLE_RATE        8000u
#define G723_PCM_SAMPLES_PER_FRAME  240u
#define G723_PCM_SAMPLE_RATE_16K    16000u
#define G723_PCM_SAMPLES_PER_FRAME_16K 480u

#define G723_FRAME_BYTES_5300       20u
#define G723_FRAME_BYTES_6300       24u
#define G723_FRAME_BYTES_SID        4u
#define G723_FRAME_BYTES_UNTRANSMITTED 1u

typedef enum {
    G723_RESULT_OK = 0,
    G723_RESULT_INVALID_ARGUMENT = -1,
    G723_RESULT_UNSUPPORTED = -2,
    G723_RESULT_BAD_FRAME = -3,
    G723_RESULT_NO_MEMORY = -4,
    G723_RESULT_INTERNAL = -5
} g723_result_t;

typedef enum {
    G723_BITRATE_5300 = 5300,
    G723_BITRATE_6300 = 6300
} g723_bitrate_t;

typedef enum {
    G723_FRAME_TYPE_6300 = 0,
    G723_FRAME_TYPE_5300 = 1,
    G723_FRAME_TYPE_SID = 2,
    G723_FRAME_TYPE_UNTRANSMITTED = 3,
    G723_FRAME_TYPE_INVALID = 255
} g723_frame_type_t;

/*
 * Basic library/backend information.
 */
G723_EXPORT uint32_t g723_abi_version(void);

G723_EXPORT const char *g723_backend_name(void);

G723_EXPORT int32_t g723_backend_supports_bitrate(
    int32_t bitrate
);

/*
 * Encoder.
 *
 * encoder_out receives an opaque native handle.
 */
G723_EXPORT int32_t g723_encoder_create(
    int32_t bitrate,
    void **encoder_out
);

G723_EXPORT void g723_encoder_destroy(
    void *encoder
);

/*
 * pcm must contain exactly 240 signed 16-bit samples at 8000 Hz.
 *
 * output_capacity should normally be at least 24 bytes.
 */
G723_EXPORT int32_t g723_encode_frame(
    void *encoder,
    const int16_t *pcm,
    uint32_t pcm_samples,
    uint8_t *output,
    uint32_t output_capacity,
    uint32_t *output_size
);

G723_EXPORT int32_t g723_encoder_set_sample_rate(
    void *encoder,
    uint32_t sample_rate
);

/*
 * Decoder.
 */
G723_EXPORT int32_t g723_decoder_create(
    void **decoder_out
);

G723_EXPORT void g723_decoder_destroy(
    void *decoder
);

G723_EXPORT int32_t g723_decoder_set_sample_rate(
    void *decoder,
    uint32_t sample_rate
);

/*
 * Decodes one complete G.723.1 frame.
 *
 * pcm_capacity is measured in samples, not bytes, and must be
 * at least 240.
 *
 * pcm_samples receives the number of decoded samples.
 */
G723_EXPORT int32_t g723_decode_frame(
    void *decoder,
    const uint8_t *input,
    uint32_t input_size,
    int16_t *pcm,
    uint32_t pcm_capacity,
    uint32_t *pcm_samples
);

/*
 * Frame inspection.
 */
G723_EXPORT int32_t g723_frame_type(
    const uint8_t *input,
    uint32_t input_size
);

G723_EXPORT uint32_t g723_frame_size(
    int32_t frame_type
);

#ifdef __cplusplus
}
#endif

#endif
